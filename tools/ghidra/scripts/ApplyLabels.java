// Applies curated names and class sizes from analysis/labels/*.tsv.
// The label files are the source of truth for every name they contain: each row is
// re-applied on every run and wins over analysis names. Each row carries the
// evidence it was derived from.
//
//   *.functions.tsv  address  namespace  name  calling_convention  evidence
//       Creates the function if needed. namespace is "Global" or a :: path; class
//       namespaces must already exist (from RTTI or a classes file). calling_convention
//       may be empty. An address inside another function's body (a tail-jump target that
//       analysis merged into its caller) is split off into a function of its own.
//   *.data.tsv       address  namespace  name  evidence  [type]
//       A label on data, such as a global object. The optional type is one of the
//       field types below, or class:<Class> for the class struct itself, and is applied
//       as data at the address. A class struct is applied only once it has a size and
//       when no other data label lies inside it.
//   *.classes.tsv    class  size  evidence
//       Creates the class if it does not exist (classes without RTTI) and grows its
//       struct to size bytes, which may be empty; it never shrinks one.
//   *.fields.tsv     class  offset  size  type  name  evidence
//       Rebuilds each listed class struct from these fields, keeping its size and the
//       vftable fields ApplyRtti put there. Types: byte word dword qword int float
//       double pointer ptr:<Class> std_string std_list:<elem size> std_tree:<value size>
//       class:<Class>
//       array:<elem>:<count>,
//       where elem is class:<Class> or bytes<N>. Member objects and arrays are applied
//       as byte ranges with their type in the field comment.
//       std_string, std_list and std_tree are the structs of the std::string, std::list
//       and std::Tree classes (MSVC 6 layouts), which this script creates, so `this` is
//       typed in the container methods.
// Kinds are applied in this order: classes, fields, functions, data. Within a kind,
// generated files (those with a "# generated" line) come first and curated files last,
// so a curated name wins over a generated one at the same address.
// Arguments: <labels directory>
//@category Ragexe

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;

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
	private final Set<Address> labelledFunctions = new HashSet<>();
	private final TreeSet<Address> dataLabels = new TreeSet<>();
	private int untypedData = 0;

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
		List<File> ordered = new ArrayList<>();
		for (boolean generated : new boolean[] { true, false }) {
			for (File f : files) {
				if (isGenerated(f) == generated) {
					ordered.add(f);
				}
			}
		}
		files = ordered.toArray(new File[0]);
		for (GhidraClass c : iterable(currentProgram.getSymbolTable().getClassNamespaces())) {
			classesByPath.put(c.getName(true), c);
		}
		DataTypeManager containers = currentProgram.getDataTypeManager();
		stdString(containers);
		stdList(containers);
		stdTree(containers);
		for (File f : files) {
			if (f.getName().endsWith(".data.tsv")) {
				for (String[] r : read(f)) {
					dataLabels.add(toAddr(Long.parseLong(r[0], 16)));
				}
			}
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
		int reset = resetDroppedPlaceholders();
		removeLegacyContainerTypes(currentProgram.getDataTypeManager());
		removeRenamedNamespaces(currentProgram.getDataTypeManager());
		println(String.format("labels: %d classes, %d fields, %d functions, %d data labels applied", classes,
			fields, functions, data));
		if (reset > 0) {
			println(reset + " method_ placeholders no label file names any more were reset to default names");
		}
		if (untypedData > 0) {
			println(untypedData + " data labels left untyped: their class struct has no size yet");
		}
		for (String w : warnings) {
			println("warning: " + w);
		}
	}

	/**
	 * A method_<address> name comes only from recover_layouts' generated rows. When a run
	 * stops proposing one, the function goes back to its default name instead of keeping
	 * the placeholder from the run before.
	 */
	private int resetDroppedPlaceholders() throws Exception {
		int n = 0;
		for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
			Symbol s = fn.getSymbol();
			if (s.getSource() == SourceType.USER_DEFINED && s.getName().matches("method_[0-9a-f]{8}")
					&& !labelledFunctions.contains(fn.getEntryPoint())) {
				s.setNameAndNamespace(null, currentProgram.getGlobalNamespace(), SourceType.DEFAULT);
				n++;
			}
		}
		return n;
	}

	private static boolean isGenerated(File f) throws Exception {
		for (String line : Files.readAllLines(f.toPath(), StandardCharsets.UTF_8)) {
			if (line.startsWith("# generated")) {
				return true;
			}
		}
		return false;
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
				fn = splitOff(container, a);
			}
			else {
				disassemble(a);
				fn = createFunction(a, null);
			}
			if (fn == null) {
				warnings.add(file + ": could not create a function at " + r[0]);
				return false;
			}
		}
		labelledFunctions.add(a);
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

	/**
	 * Makes a function at a, which analysis put inside container (typically the target of
	 * a tail jump). The container is recreated afterwards, so its body now stops at the
	 * jump; its name, namespace and calling convention are kept.
	 */
	private Function splitOff(Function container, Address a) throws Exception {
		Address entry = container.getEntryPoint();
		String name = container.getName();
		Namespace ns = container.getParentNamespace();
		SourceType source = container.getSymbol().getSource();
		String cc = container.getCallingConventionName();
		removeFunction(container);
		Function fn = createFunction(a, null);
		Function again = createFunction(entry, null);
		if (again != null && source != SourceType.DEFAULT) {
			again.getSymbol().setNameAndNamespace(name, ns, source);
			again.setCallingConvention(cc);
		}
		if (again == null) {
			warnings.add("could not recreate " + name + " at " + entry + " after splitting off " + a);
		}
		return fn;
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
		if (r.length > 4 && !r[4].isEmpty()) {
			DataType dt;
			if (r[4].startsWith("class:")) {
				GhidraClass c = classesByPath.get(r[4].substring(6));
				if (c == null) {
					warnings.add(file + ": " + r[0] + " " + r[2] + ": no class " + r[4].substring(6));
					return true;
				}
				dt = classStruct(c, currentProgram.getDataTypeManager());
				if (dt.isZeroLength()) {
					untypedData++;
					return true;
				}
			}
			else {
				dt = fieldType(r[4], 0, currentProgram.getDataTypeManager());
			}
			Address end = a.add(dt.getLength() - 1);
			Address inside = dataLabels.higher(a);
			if (inside != null && inside.compareTo(end) <= 0) {
				warnings.add(file + ": " + r[2] + " (" + r[4] + ", " + dt.getLength() + " bytes) would cover the label at " + inside);
				return true;
			}
			currentProgram.getListing().clearCodeUnits(a, end, false);
			createData(a, dt);
		}
		return true;
	}

	private boolean applyClassSize(String file, String[] r) throws Exception {
		Namespace ns = classOrNamespace(r[0]);
		if (!(ns instanceof GhidraClass)) {
			ns = NamespaceUtils.convertNamespaceToClass(ns);
			classesByPath.put(r[0], (GhidraClass) ns);
		}
		if (r[1].isEmpty()) {
			return true;
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

	private DataType fieldType(String type, int size, DataTypeManager dtm) throws Exception {
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
			case "std_tree":
				return stdTree(dtm);
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

		/** MSVC 6 std::string: allocator byte, then the copy-on-write buffer, length and capacity. */
	private DataType stdString(DataTypeManager dtm) throws Exception {
		StructureDataType s = new StructureDataType("string", 0, dtm);
		s.add(ByteDataType.dataType, "allocator", "empty");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 3, 1), "_pad", null);
		s.add(new PointerDataType(CharDataType.dataType, dtm), "buffer",
			"characters; the byte before them is the copy-on-write reference count");
		s.add(DWordDataType.dataType, "length", null);
		s.add(DWordDataType.dataType, "capacity", null);
		return containerClass("std::string", s, dtm);
	}

	/** MSVC 6 std::list: allocator byte, then the sentinel node and the element count. */
	private DataType stdList(DataTypeManager dtm) throws Exception {
		StructureDataType s = new StructureDataType("list", 0, dtm);
		s.add(ByteDataType.dataType, "allocator", "empty");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 3, 1), "_pad", null);
		s.add(PointerDataType.dataType, "head", "sentinel node: next, prev, then the value");
		s.add(DWordDataType.dataType, "size", null);
		return containerClass("std::list", s, dtm);
	}

	/** MSVC 6 std::Tree, the red-black tree of map and set: allocator and comparator bytes, head node, multi flag, size. */
	private DataType stdTree(DataTypeManager dtm) throws Exception {
		StructureDataType s = new StructureDataType("Tree", 0, dtm);
		s.add(ByteDataType.dataType, "allocator", "empty");
		s.add(ByteDataType.dataType, "compare", "the key comparison object (empty)");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 2, 1), "_pad", null);
		s.add(PointerDataType.dataType, "head", "head node: left, parent, right, value, color");
		s.add(ByteDataType.dataType, "multi", "true for multimap and multiset");
		s.add(new ArrayDataType(Undefined1DataType.dataType, 3, 1), "_pad2", null);
		s.add(DWordDataType.dataType, "size", null);
		return containerClass("std::Tree", s, dtm);
	}

	/**
	 * Makes the container namespace a class whose struct (at the class's own category
	 * path, which is what Ghidra types `this` with) has the given layout. All template
	 * instances share one class: the layout does not depend on the element type.
	 */
	private Structure containerClass(String path, StructureDataType layout, DataTypeManager dtm)
			throws Exception {
		GhidraClass c = classesByPath.get(path);
		if (c == null) {
			Namespace ns = namespace(path);
			c = ns instanceof GhidraClass ? (GhidraClass) ns : NamespaceUtils.convertNamespaceToClass(ns);
			classesByPath.put(path, c);
		}
		Structure s = classStruct(c, dtm);
		if (!sameLayout(s, layout)) {
			s.replaceWith(layout);
			s.setDescription("MSVC 6 Dinkumware " + path + "; applied by ApplyLabels");
		}
		return s;
	}

	private static boolean sameLayout(Structure a, Structure b) {
		if (a.getLength() != b.getLength() || a.getNumComponents() != b.getNumComponents()) {
			return false;
		}
		for (int k = 0; k < a.getNumComponents(); k++) {
			DataTypeComponent x = a.getComponent(k), y = b.getComponent(k);
			if (x.getOffset() != y.getOffset() || !java.util.Objects.equals(x.getFieldName(), y.getFieldName())
				|| !x.getDataType().isEquivalent(y.getDataType())) {
				return false;
			}
		}
		return true;
	}

	/** Container structs that earlier versions kept in /Ragexe/stl. */
	private void removeLegacyContainerTypes(DataTypeManager dtm) {
		CategoryPath cat = new CategoryPath("/Ragexe/stl");
		for (String name : new String[] { "std_string", "std_list", "std_tree" }) {
			DataType dt = dtm.getDataType(cat, name);
			if (dt != null) {
				dtm.remove(dt);
			}
		}
		ghidra.program.model.data.Category c = dtm.getCategory(cat);
		if (c != null && c.getDataTypes().length == 0 && c.getCategories().length == 0) {
			dtm.getCategory(cat.getParent()).removeEmptyCategory(cat.getName(), monitor);
		}
	}

	/**
	 * Namespaces that docs/naming.md renamed (library-internal names): once no symbol is
	 * left in one, it and its class struct are removed.
	 */
	private void removeRenamedNamespaces(DataTypeManager dtm) throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		for (String path : new String[] { "std::_Tree", "std::_Lockit", "std::_Locinfo" }) {
			List<Namespace> found = NamespaceUtils.getNamespaceByPath(currentProgram, null, path);
			if (found.isEmpty() || st.getChildren(found.get(0).getSymbol()).hasNext()) {
				continue;
			}
			Namespace ns = found.get(0);
			DataType dt = dtm.getDataType(new CategoryPath("/std"), ns.getName());
			if (dt != null) {
				dtm.remove(dt);
			}
			ns.getSymbol().delete();
		}
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
