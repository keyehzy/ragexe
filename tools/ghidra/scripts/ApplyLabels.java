// Applies curated names and class sizes from analysis/labels/*.tsv.
// The label files are the source of truth for every name they contain: each row is
// re-applied on every run and wins over analysis names. Each row carries the
// evidence it was derived from.
//
//   *.functions.tsv  address  namespace  name  calling_convention  evidence
//       Creates the function if needed. namespace is "Global" or a :: path; class
//       namespaces must already exist (from RTTI). calling_convention may be empty.
//   *.data.tsv       address  namespace  name  evidence
//       A label on data, such as a global object.
//   *.classes.tsv    class  size  evidence
//       Grows the class struct to size bytes; it never shrinks one.
// Arguments: <labels directory>
//@category Ragexe

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyLabels extends GhidraScript {

	private final List<String> warnings = new ArrayList<>();

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File dir = new File(args.length > 0 ? args[0] : "analysis/labels");
		File[] files = dir.listFiles((d, n) -> n.endsWith(".tsv"));
		if (files == null) {
			println("no label directory at " + dir.getAbsolutePath());
			return;
		}
		Arrays.sort(files);
		int functions = 0, data = 0, classes = 0;
		for (File f : files) {
			List<String[]> rows = read(f);
			if (f.getName().endsWith(".functions.tsv")) {
				for (String[] r : rows) {
					functions += applyFunction(f.getName(), r) ? 1 : 0;
				}
			}
			else if (f.getName().endsWith(".data.tsv")) {
				for (String[] r : rows) {
					data += applyData(f.getName(), r) ? 1 : 0;
				}
			}
			else if (f.getName().endsWith(".classes.tsv")) {
				for (String[] r : rows) {
					classes += applyClassSize(f.getName(), r) ? 1 : 0;
				}
			}
		}
		println(String.format("labels: %d functions, %d data labels, %d class sizes applied", functions, data,
			classes));
		for (String w : warnings) {
			println("warning: " + w);
		}
	}

	private List<String[]> read(File f) throws Exception {
		List<String[]> rows = new ArrayList<>();
		List<String> lines = Files.readAllLines(f.toPath(), StandardCharsets.UTF_8);
		for (String line : lines.subList(1, lines.size())) {
			if (!line.isBlank() && !line.startsWith("#")) {
				rows.add(line.split("\t", -1));
			}
		}
		return rows;
	}

	private boolean applyFunction(String file, String[] r) throws Exception {
		Address a = toAddr(Long.parseLong(r[0], 16));
		Namespace ns = namespace(r[1]);
		Function fn = getFunctionAt(a);
		if (fn == null) {
			Function container = getFunctionContaining(a);
			if (container != null) {
				warnings.add(file + ": " + r[0] + " lies inside " + container.getName(true));
				return false;
			}
			disassemble(a);
			fn = createFunction(a, null);
			if (fn == null) {
				warnings.add(file + ": could not create a function at " + r[0]);
				return false;
			}
		}
		try {
			fn.getSymbol().setNameAndNamespace(r[2], ns, SourceType.USER_DEFINED);
		}
		catch (Exception e) {
			warnings.add(file + ": " + r[0] + " " + r[2] + ": " + e.getMessage());
			return false;
		}
		if (r.length > 3 && !r[3].isEmpty() && !r[3].equals(fn.getCallingConventionName())) {
			fn.setCallingConvention(r[3]);
		}
		return true;
	}

	private boolean applyData(String file, String[] r) throws Exception {
		Address a = toAddr(Long.parseLong(r[0], 16));
		Namespace ns = namespace(r[1]);
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol s = st.getSymbol(r[2], a, ns);
		if (s == null) {
			s = st.createLabel(a, r[2], ns, SourceType.USER_DEFINED);
		}
		s.setPrimary();
		return true;
	}

	private boolean applyClassSize(String file, String[] r) throws Exception {
		Namespace ns = namespace(r[0]);
		if (!(ns instanceof GhidraClass)) {
			warnings.add(file + ": " + r[0] + " is not a class");
			return false;
		}
		int size = Integer.parseInt(r[1]);
		Structure s = classStruct((GhidraClass) ns, currentProgram.getDataTypeManager());
		int have = s.isZeroLength() ? 0 : s.getLength();
		if (have > size) {
			warnings.add(file + ": " + r[0] + " struct is already " + have + " bytes, larger than " + size);
			return false;
		}
		if (have < size) {
			s.growStructure(size - have);
		}
		return true;
	}

	/** The class struct at the namespace's own category path; same rule as ApplyRtti. */
	private static Structure classStruct(GhidraClass ns, DataTypeManager dtm) {
		List<String> parts = new ArrayList<>();
		for (Namespace p = ns.getParentNamespace(); p != null && !p.isGlobal(); p = p.getParentNamespace()) {
			parts.add(0, p.getName());
		}
		CategoryPath cat = CategoryPath.ROOT;
		for (String part : parts) {
			cat = new CategoryPath(cat, part);
		}
		DataType dt = dtm.getDataType(cat, ns.getName());
		if (dt instanceof Structure) {
			return (Structure) dt;
		}
		return (Structure) dtm.addDataType(new StructureDataType(cat, ns.getName(), 0, dtm),
			DataTypeConflictHandler.KEEP_HANDLER);
	}

	private Namespace namespace(String path) throws Exception {
		if (path.isEmpty() || path.equals("Global")) {
			return currentProgram.getGlobalNamespace();
		}
		List<Namespace> found = NamespaceUtils.getNamespaceByPath(currentProgram, null, path);
		if (!found.isEmpty()) {
			return found.get(0);
		}
		return NamespaceUtils.createNamespaceHierarchy(path, null, currentProgram, SourceType.USER_DEFINED);
	}
}
