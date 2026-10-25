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
//   *.fields.tsv     class  offset  size  type  name  evidence
//       Rebuilds each listed class struct from these fields, keeping its size and the
//       vftable fields ApplyRtti put there. Types: byte word dword qword int float
//       double pointer ptr:<Class> std_string std_list:<elem size> class:<Class>
//       array:<elem>:<count>,
//       where elem is class:<Class> or bytes<N>. Member objects and arrays are applied
//       as byte ranges with their type in the field comment.
// Kinds are applied in this order: classes, fields, functions, data.
// Arguments: <labels directory>
//@category Ragexe

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.ByteDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.CharDataType;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DoubleDataType;
import ghidra.program.model.data.FloatDataType;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.QWordDataType;
import ghidra.program.model.data.ShortDataType;
import ghidra.program.model.data.Undefined1DataType;
import ghidra.program.model.data.WordDataType;
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
	private final Map<String, GhidraClass> classesByPath = new HashMap<>();

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
		for (GhidraClass c : iterable(currentProgram.getSymbolTable().getClassNamespaces())) {
			classesByPath.put(c.getName(true), c);
		}
		int functions = 0, data = 0, classes = 0, fields = 0;
		for (String kind : new String[] { ".classes.tsv", ".fields.tsv", ".functions.tsv", ".data.tsv" }) {
			for (File f : files) {
				if (!f.getName().endsWith(kind)) {
					continue;
				}
				List<String[]> rows = read(f);
				switch (kind) {
					case ".classes.tsv":
						for (String[] r : rows) {
							classes += applyClassSize(f.getName(), r) ? 1 : 0;
						}
						break;
					case ".fields.tsv":
						fields += applyFields(f.getName(), rows);
						break;
					case ".functions.tsv":
						for (String[] r : rows) {
							functions += applyFunction(f.getName(), r) ? 1 : 0;
						}
						break;
					default:
						for (String[] r : rows) {
							data += applyData(f.getName(), r) ? 1 : 0;
						}
				}
			}
		}
		println(String.format("labels: %d class sizes, %d fields, %d functions, %d data labels applied", classes,
			fields, functions, data));
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
			try {
				fn.setCallingConvention(r[3]);
			}
			catch (Exception e) {
				warnings.add(file + ": " + r[0] + " calling convention '" + r[3] + "': " + e.getMessage());
			}
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
		Namespace ns = classOrNamespace(r[0]);
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

	private int applyFields(String file, List<String[]> rows) throws Exception {
		Map<String, List<String[]>> byClass = new LinkedHashMap<>();
		for (String[] r : rows) {
			byClass.computeIfAbsent(r[0], k -> new ArrayList<>()).add(r);
		}
		DataTypeManager dtm = currentProgram.getDataTypeManager();
		int applied = 0;
		for (Map.Entry<String, List<String[]>> e : byClass.entrySet()) {
			GhidraClass cls = classesByPath.get(e.getKey());
			if (cls == null) {
				warnings.add(file + ": no class " + e.getKey());
				continue;
			}
			Structure s = classStruct(cls, dtm);
			int length = s.isZeroLength() ? 0 : s.getLength();
			List<DataTypeComponent> vftables = new ArrayList<>();
			for (DataTypeComponent c : s.getDefinedComponents()) {
				if (c.getFieldName() != null && c.getFieldName().startsWith("vftable")) {
					vftables.add(c);
				}
			}
			List<Object[]> keep = new ArrayList<>();
			for (DataTypeComponent c : vftables) {
				keep.add(new Object[] { c.getOffset(), c.getDataType(), c.getFieldName(), c.getComment() });
			}
			s.deleteAll();
			if (length > 0) {
				s.growStructure(length);
			}
			for (Object[] k : keep) {
				s.replaceAtOffset((Integer) k[0], (DataType) k[1], 4, (String) k[2], (String) k[3]);
			}
			for (String[] r : e.getValue()) {
				int off = Integer.decode(r[1]);
				int size = Integer.parseInt(r[2]);
				DataType dt = fieldType(r[3], size, dtm);
				if (off + size > s.getLength()) {
					warnings.add(file + ": " + e.getKey() + " field " + r[1] + " ends past the struct");
					continue;
				}
				try {
					String comment = r[3].contains(":") ? r[3] + "; " + r[5] : r[5];
					s.replaceAtOffset(off, dt, size, r[4], comment);
					applied++;
				}
				catch (Exception ex) {
					warnings.add(file + ": " + e.getKey() + " field " + r[1] + ": " + ex.getMessage());
				}
			}
		}
		return applied;
	}

	private DataType fieldType(String type, int size, DataTypeManager dtm) {
		String[] p = type.split(":", 2);
		switch (p[0]) {
			case "byte":
				return ByteDataType.dataType;
			case "word":
				return WordDataType.dataType;
			case "dword":
				return DWordDataType.dataType;
			case "qword":
				return QWordDataType.dataType;
			case "int":
				return size == 2 ? ShortDataType.dataType : IntegerDataType.dataType;
			case "float":
				return FloatDataType.dataType;
			case "double":
				return DoubleDataType.dataType;
			case "pointer":
				return PointerDataType.dataType;
			case "std_string":
				return stdString(dtm);
			case "std_list":
				return stdList(dtm);
			case "ptr": {
				GhidraClass c = classesByPath.get(p[1]);
				return c == null ? PointerDataType.dataType : new PointerDataType(classStruct(c, dtm), dtm);
			}
			case "class":
				// Member objects stay as bytes: embedding another class struct would be
				// resized while that class is rebuilt. The type goes in the comment.
				return undefinedBytes(size);
			case "array": {
				int sep = p[1].lastIndexOf(':');
				String elem = p[1].substring(0, sep);
				int count = Integer.parseInt(p[1].substring(sep + 1));
				int esize = size / count;
				DataType et = undefinedBytes(esize);
				return new ArrayDataType(et, count, esize, dtm);
			}
			default:
				return undefinedBytes(size);
		}
	}

	private static DataType undefinedBytes(int size) {
		return size == 1 ? Undefined1DataType.dataType
			: new ArrayDataType(Undefined1DataType.dataType, size, 1);
	}

	/** MSVC 6 std::string: allocator byte, then pointer, length and capacity. */
	private DataType stdString(DataTypeManager dtm) {
		CategoryPath cat = new CategoryPath("/Ragexe/stl");
		DataType dt = dtm.getDataType(cat, "std_string");
		if (dt != null) {
			return dt;
		}
		StructureDataType s = new StructureDataType(cat, "std_string", 0, dtm);
		s.add(ByteDataType.dataType, "_Al", "allocator (empty)");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 3, 1), "_pad", null);
		s.add(new PointerDataType(CharDataType.dataType, dtm), "_Ptr",
			"characters; the byte before them is the copy-on-write reference count");
		s.add(DWordDataType.dataType, "_Len", null);
		s.add(DWordDataType.dataType, "_Res", "capacity");
		return dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
	}

	/** MSVC 6 std::list: allocator byte, then the sentinel node and the element count. */
	private DataType stdList(DataTypeManager dtm) {
		CategoryPath cat = new CategoryPath("/Ragexe/stl");
		DataType dt = dtm.getDataType(cat, "std_list");
		if (dt != null) {
			return dt;
		}
		StructureDataType s = new StructureDataType(cat, "std_list", 0, dtm);
		s.add(ByteDataType.dataType, "_Al", "allocator (empty)");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 3, 1), "_pad", null);
		s.add(PointerDataType.dataType, "_Head", "sentinel node: next, prev, then the value");
		s.add(DWordDataType.dataType, "_Size", null);
		return dtm.addDataType(s, DataTypeConflictHandler.KEEP_HANDLER);
	}

	private Namespace classOrNamespace(String path) throws Exception {
		GhidraClass c = classesByPath.get(path);
		return c != null ? c : namespace(path);
	}

	private static <T> Iterable<T> iterable(java.util.Iterator<T> it) {
		return () -> it;
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
