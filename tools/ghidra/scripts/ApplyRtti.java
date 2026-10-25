// Recovers MSVC RTTI: complete object locators, class hierarchies and vtables.
// Applies typed RTTI structures, vtable structs and class-struct vfptr fields, creates
// functions at vtable slot targets, and moves each virtual function into the class
// that introduces it. Safe to re-run: it only renames functions it named itself or
// that still carry a default name.
// Arguments: <output directory>
// Writes <program>.rtti.tsv, <program>.vtables.tsv and <program>.vtable_refs.tsv.
//@category Ragexe

import java.io.File;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.CategoryPath;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.DWordDataType;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.IntegerDataType;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.StructureDataType;
import ghidra.program.model.data.Undefined;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;

public class ApplyRtti extends GhidraScript {

	private static final CategoryPath RTTI_CAT = new CategoryPath("/Ragexe/RTTI");
	private static final CategoryPath VFT_CAT = new CategoryPath("/Ragexe/vftables");
	private static final CategoryPath SLOT_CAT = new CategoryPath("/Ragexe/vftables/slots");
	private static final Pattern OUR_NAME = Pattern.compile("vf\\d+(_for_.+)?");

	private static class BaseDesc {
		Address addr;
		Address td;
		int contained;
		int mdisp, pdisp, vdisp, attributes;
	}

	private static class ClassInfo {
		Address td;
		GhidraClass ns;
		String mangled;
		int chdAttributes;
		Address chd;
		List<BaseDesc> bases = new ArrayList<>(); // CHD order; bases.get(0) is the class itself
		Map<Integer, VTable> vtables = new TreeMap<>(); // keyed by vfptr offset
	}

	private static class VTable {
		ClassInfo cls;
		Address col;
		Address start;
		int offset;
		String label;
		List<Address> slots = new ArrayList<>();
	}

	private MemoryBlock text, rdata;
	private Map<Address, ClassInfo> byTd;
	private DataTypeManager dtm;
	private SymbolTable symtab;
	private final List<String> warnings = new ArrayList<>();

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : ".");
		outDir.mkdirs();
		text = currentProgram.getMemory().getBlock(".text");
		rdata = currentProgram.getMemory().getBlock(".rdata");
		dtm = currentProgram.getDataTypeManager();
		symtab = currentProgram.getSymbolTable();

		byTd = collectTypeDescriptors();
		Map<Address, VTable> byCol = collectLocators(byTd);
		collectVtables(byCol);
		byCol.values().removeIf(vt -> vt.start == null);

		DataType colType = colType(), chdType = chdType(), bcdType = bcdType();
		for (ClassInfo c : byTd.values()) {
			if (c.chd != null) {
				applyHierarchy(c, chdType, bcdType, byTd);
			}
		}
		for (VTable vt : byCol.values()) {
			applyData(vt.col, colType);
			label(vt.col, vt.offset == 0 || vt.cls.vtables.size() == 1 ? "RTTI_Complete_Object_Locator"
				: "RTTI_Complete_Object_Locator_" + vt.label.substring("vftable_".length()), vt.cls.ns);
		}

		int created = createSlotFunctions(byCol.values());
		Map<Address, String> notes = new HashMap<>();
		int named = assignOwners(byCol.values(), byTd, notes);
		int structs = 0;
		for (ClassInfo c : byTd.values()) {
			for (VTable vt : c.vtables.values()) {
				DataType vtType = applyVtable(vt);
				if (addVfptr(c, vt, vtType)) {
					structs++;
				}
			}
		}

		writeExports(outDir, byTd, byCol, notes);
		int slots = byCol.values().stream().mapToInt(v -> v.slots.size()).sum();
		println(String.format("RTTI: %d type descriptors, %d locators, %d vtables, %d slots", byTd.size(),
			byCol.size(), byCol.size(), slots));
		println(String.format("functions created at slot targets: %d; virtual functions named: %d; " +
			"class-struct vfptr fields set: %d", created, named, structs));
		for (String w : warnings) {
			println("warning: " + w);
		}
	}

	// ---- discovery ---------------------------------------------------------------

	private Map<Address, ClassInfo> collectTypeDescriptors() throws Exception {
		Map<Address, ClassInfo> byTd = new TreeMap<>();
		for (Symbol s : symtab.getSymbolIterator("RTTI_Type_Descriptor", true)) {
			ClassInfo c = new ClassInfo();
			c.td = s.getAddress();
			c.mangled = readCString(c.td.add(8));
			Namespace ns = s.getParentNamespace();
			c.ns = ns instanceof GhidraClass ? (GhidraClass) ns : NamespaceUtils.convertNamespaceToClass(ns);
			byTd.put(c.td, c);
		}
		return byTd;
	}

	private Map<Address, VTable> collectLocators(Map<Address, ClassInfo> byTd) throws Exception {
		Map<Address, VTable> byCol = new TreeMap<>();
		for (Address a = rdata.getStart(); a.compareTo(rdata.getEnd().subtract(20)) < 0; a = a.add(4)) {
			if (getInt(a) != 0) {
				continue;
			}
			ClassInfo c = byTd.get(toAddr(getInt(a.add(12)) & 0xffffffffL));
			Address chd = toAddr(getInt(a.add(16)) & 0xffffffffL);
			if (c == null || !rdata.contains(chd) || getInt(chd) != 0) {
				continue;
			}
			int nBases = getInt(chd.add(8));
			if (nBases < 1 || nBases > 64) {
				continue;
			}
			VTable vt = new VTable();
			vt.cls = c;
			vt.col = a;
			vt.offset = getInt(a.add(4));
			byCol.put(a, vt);
			if (c.chd == null) {
				c.chd = chd;
				c.chdAttributes = getInt(chd.add(4));
				Address bca = toAddr(getInt(chd.add(12)) & 0xffffffffL);
				for (int i = 0; i < nBases; i++) {
					BaseDesc b = new BaseDesc();
					b.addr = toAddr(getInt(bca.add(4 * i)) & 0xffffffffL);
					b.td = toAddr(getInt(b.addr) & 0xffffffffL);
					b.contained = getInt(b.addr.add(4));
					b.mdisp = getInt(b.addr.add(8));
					b.pdisp = getInt(b.addr.add(12));
					b.vdisp = getInt(b.addr.add(16));
					b.attributes = getInt(b.addr.add(20));
					c.bases.add(b);
				}
			}
			else if (!c.chd.equals(chd)) {
				warnings.add(c.ns.getName(true) + " has locators with different hierarchy descriptors");
			}
		}
		return byCol;
	}

	private void collectVtables(Map<Address, VTable> byCol) throws Exception {
		Set<Address> starts = new LinkedHashSet<>();
		for (Address a = rdata.getStart(); a.compareTo(rdata.getEnd().subtract(4)) < 0; a = a.add(4)) {
			VTable vt = byCol.get(toAddr(getInt(a) & 0xffffffffL));
			if (vt == null) {
				continue;
			}
			if (vt.start != null) {
				warnings.add("locator " + vt.col + " is referenced by more than one vtable");
				continue;
			}
			vt.start = a.add(4);
			starts.add(vt.start);
			vt.cls.vtables.put(vt.offset, vt);
		}
		for (VTable vt : byCol.values()) {
			if (vt.start == null) {
				warnings.add("locator " + vt.col + " (" + vt.cls.ns.getName(true) + ") has no vtable");
				continue;
			}
			for (Address a = vt.start;; a = a.add(4)) {
				if (starts.contains(a.add(4))) {
					break; // a holds the next vtable's locator pointer
				}
				if (!a.equals(vt.start) && hasBoundaryReference(a)) {
					break;
				}
				Address target = toAddr(getInt(a) & 0xffffffffL);
				if (!text.contains(target)) {
					break;
				}
				vt.slots.add(target);
			}
		}
		for (VTable vt : byCol.values()) {
			if (vt.start != null) {
				vt.label = vt.cls.vtables.size() == 1 || vt.offset == 0 ? "vftable"
					: "vftable_for_" + secondaryBaseName(vt.cls, vt.offset);
			}
		}
		checkAgainstPrimaryBases(byCol);
	}

	/**
	 * A reference ends a vtable only if the referencing instruction names the address
	 * itself (a data pointer or an absolute operand such as FLD [addr]). References that
	 * analysis derived from a known register value, as in a devirtualized CALL [EAX+0x18],
	 * point into the middle of a vtable and are not boundaries.
	 */
	private boolean hasBoundaryReference(Address a) throws Exception {
		long want = a.getOffset();
		for (Reference r : getReferencesTo(a)) {
			Instruction insn = getInstructionAt(r.getFromAddress());
			if (insn == null) {
				return true;
			}
			byte[] bytes = insn.getBytes();
			for (int i = 0; i + 4 <= bytes.length; i++) {
				long v = (bytes[i] & 0xffL) | (bytes[i + 1] & 0xffL) << 8 | (bytes[i + 2] & 0xffL) << 16 |
					(bytes[i + 3] & 0xffL) << 24;
				if (v == want) {
					return true;
				}
			}
		}
		return false;
	}

	/** A class's primary vtable repeats its primary base's slots, so it cannot be shorter. */
	private void checkAgainstPrimaryBases(Map<Address, VTable> byCol) {
		for (VTable vt : byCol.values()) {
			if (vt.start == null || vt.offset != 0 || vt.cls.bases.size() < 2) {
				continue;
			}
			BaseDesc primary = vt.cls.bases.get(1);
			ClassInfo base = byTd.get(primary.td);
			if (primary.mdisp != 0 || primary.pdisp >= 0 || base == null) {
				continue;
			}
			VTable bvt = base.vtables.get(0);
			if (bvt != null && bvt.slots.size() > vt.slots.size()) {
				warnings.add(String.format("%s vtable has %d slots but its primary base %s has %d",
					vt.cls.ns.getName(true), vt.slots.size(), base.ns.getName(true), bvt.slots.size()));
			}
		}
	}

	/** The most-derived base whose vfptr shares this offset, used to name a secondary vtable. */
	private String secondaryBaseName(ClassInfo c, int offset) {
		BaseDesc best = null;
		for (BaseDesc b : c.bases.subList(1, c.bases.size())) {
			boolean here = b.pdisp < 0 ? b.mdisp == offset : true;
			if (here && (best == null || b.contained > best.contained)) {
				best = b;
			}
		}
		ClassInfo base = best == null ? null : byTd.get(best.td);
		return base == null ? "at_" + offset : sanitize(base.ns.getName(false));
	}

	// ---- RTTI data ---------------------------------------------------------------

	private DataType colType() {
		StructureDataType s = new StructureDataType(RTTI_CAT, "RTTICompleteObjectLocator", 0, dtm);
		s.add(DWordDataType.dataType, "signature", null);
		s.add(IntegerDataType.dataType, "offset", "offset of this vfptr in the complete object");
		s.add(IntegerDataType.dataType, "cdOffset", "constructor displacement offset");
		s.add(PointerDataType.dataType, "pTypeDescriptor", null);
		s.add(PointerDataType.dataType, "pClassHierarchyDescriptor", null);
		return dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
	}

	private DataType chdType() {
		StructureDataType s = new StructureDataType(RTTI_CAT, "RTTIClassHierarchyDescriptor", 0, dtm);
		s.add(DWordDataType.dataType, "signature", null);
		s.add(DWordDataType.dataType, "attributes", "bit 0: multiple inheritance, bit 1: virtual inheritance");
		s.add(DWordDataType.dataType, "numBaseClasses", "includes the class itself");
		s.add(PointerDataType.dataType, "pBaseClassArray", null);
		return dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
	}

	private DataType bcdType() {
		StructureDataType s = new StructureDataType(RTTI_CAT, "RTTIBaseClassDescriptor", 0, dtm);
		s.add(PointerDataType.dataType, "pTypeDescriptor", null);
		s.add(DWordDataType.dataType, "numContainedBases", null);
		s.add(IntegerDataType.dataType, "mdisp", "member displacement");
		s.add(IntegerDataType.dataType, "pdisp", "vbtable displacement, -1 if not a virtual base");
		s.add(IntegerDataType.dataType, "vdisp", "displacement inside the vbtable");
		s.add(DWordDataType.dataType, "attributes", null);
		return dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
	}

	private void applyHierarchy(ClassInfo c, DataType chdType, DataType bcdType, Map<Address, ClassInfo> byTd)
			throws Exception {
		applyData(c.chd, chdType);
		label(c.chd, "RTTI_Class_Hierarchy_Descriptor", c.ns);
		Address bca = toAddr(getInt(c.chd.add(12)) & 0xffffffffL);
		applyData(bca, new ArrayDataType(PointerDataType.dataType, c.bases.size(), 4));
		label(bca, "RTTI_Base_Class_Array", c.ns);
		for (BaseDesc b : c.bases) {
			applyData(b.addr, bcdType);
			ClassInfo base = byTd.get(b.td);
			if (base != null) {
				label(b.addr, String.format("RTTI_Base_Class_Descriptor_at_(%d,%d,%d,%d)", b.mdisp, b.pdisp,
					b.vdisp, b.attributes), base.ns);
			}
		}
	}

	// ---- functions -----------------------------------------------------------------

	private int createSlotFunctions(Iterable<VTable> vtables) throws Exception {
		int created = 0;
		for (VTable vt : vtables) {
			for (Address target : vt.slots) {
				if (getFunctionAt(target) != null) {
					continue;
				}
				Function container = getFunctionContaining(target);
				if (container != null) {
					warnings.add("slot target " + target + " lies inside " + container.getName(true));
					continue;
				}
				disassemble(target);
				if (createFunction(target, null) != null) {
					created++;
				}
				else {
					warnings.add("could not create a function at slot target " + target);
				}
			}
		}
		return created;
	}

	private static boolean derivesFrom(ClassInfo derived, ClassInfo base) {
		for (BaseDesc b : derived.bases) {
			if (b.td.equals(base.td)) {
				return true;
			}
		}
		return false;
	}

	/**
	 * A virtual function belongs to the most-base class among those whose vtables contain
	 * it. Functions in the vtables of unrelated classes (identical-code folding, _purecall)
	 * have no single owner and stay where they are.
	 */
	private int assignOwners(Iterable<VTable> vtables, Map<Address, ClassInfo> byTd, Map<Address, String> notes)
			throws Exception {
		Map<Address, Set<ClassInfo>> holders = new LinkedHashMap<>();
		Map<Address, List<String>> slotsOf = new HashMap<>();
		for (VTable vt : vtables) {
			for (int i = 0; i < vt.slots.size(); i++) {
				Address f = vt.slots.get(i);
				holders.computeIfAbsent(f, k -> new LinkedHashSet<>()).add(vt.cls);
			}
		}
		int named = 0;
		for (Map.Entry<Address, Set<ClassInfo>> e : holders.entrySet()) {
			Address entry = e.getKey();
			Function f = getFunctionAt(entry);
			if (f == null) {
				notes.put(entry, "not-a-function");
				continue;
			}
			ClassInfo owner = null;
			for (ClassInfo cand : e.getValue()) {
				boolean ok = true;
				for (ClassInfo other : e.getValue()) {
					if (other != cand && !derivesFrom(other, cand)) {
						ok = false;
						break;
					}
				}
				if (ok) {
					owner = cand;
					break;
				}
			}
			String adjust = adjustorDelta(entry);
			if (owner == null) {
				notes.put(entry, "shared:" + e.getValue().size() + "-unrelated-classes");
				continue;
			}
			List<String> names = new ArrayList<>();
			for (VTable vt : owner.vtables.values()) {
				for (int i = 0; i < vt.slots.size(); i++) {
					if (vt.slots.get(i).equals(entry)) {
						names.add(String.format("vf%02d", i) +
							(vt.label.equals("vftable") ? "" : vt.label.substring("vftable".length())));
					}
				}
			}
			notes.put(entry, (adjust != null ? "adjustor:" + adjust + " " : "") +
				(names.size() > 1 ? "multiple-slots" : ""));
			if (!mayRename(f)) {
				continue;
			}
			try {
				f.getSymbol().setNameAndNamespace(names.get(0), owner.ns, SourceType.ANALYSIS);
			}
			catch (Exception ex) {
				warnings.add("could not name " + entry + " " + owner.ns.getName(true) + "::" + names.get(0) +
					": " + ex.getMessage());
				continue;
			}
			if (Function.UNKNOWN_CALLING_CONVENTION_STRING.equals(f.getCallingConventionName()) ||
				Function.DEFAULT_CALLING_CONVENTION_STRING.equals(f.getCallingConventionName())) {
				f.setCallingConvention("__thiscall");
			}
			named++;
		}
		return named;
	}

	private boolean mayRename(Function f) {
		Symbol s = f.getSymbol();
		return s.getSource() == SourceType.DEFAULT ||
			(s.getSource() == SourceType.ANALYSIS && OUR_NAME.matcher(s.getName()).matches());
	}

	/** Returns the this-adjustment of an MSVC adjustor thunk (sub ecx, n; jmp target), or null. */
	private String adjustorDelta(Address a) throws Exception {
		int b0 = getByte(a) & 0xff, b1 = getByte(a.add(1)) & 0xff;
		if (b0 == 0x83 && b1 == 0xe9 && (getByte(a.add(3)) & 0xff) == 0xe9) {
			return "-" + (getByte(a.add(2)) & 0xff);
		}
		if (b0 == 0x81 && b1 == 0xe9 && (getByte(a.add(6)) & 0xff) == 0xe9) {
			return "-" + getInt(a.add(2));
		}
		return null;
	}

	// ---- vtables and class structs -----------------------------------------------

	private DataType applyVtable(VTable vt) throws Exception {
		String base = sanitize(vt.cls.ns.getName(true)) + "_" + vt.label;
		StructureDataType s = new StructureDataType(VFT_CAT, base, 0, dtm);
		for (int i = 0; i < vt.slots.size(); i++) {
			String field = String.format("vf%02d", i);
			Function f = getFunctionAt(vt.slots.get(i));
			DataType slotType = PointerDataType.dataType;
			if (f != null) {
				FunctionDefinitionDataType def =
					new FunctionDefinitionDataType(SLOT_CAT, base + "_" + field, f.getSignature(), dtm);
				slotType = new PointerDataType(dtm.addDataType(def, DataTypeConflictHandler.REPLACE_HANDLER), dtm);
			}
			s.add(slotType, 4, field, f == null ? null : f.getName(true));
		}
		DataType vtType = dtm.addDataType(s, DataTypeConflictHandler.REPLACE_HANDLER);
		if (!vt.slots.isEmpty()) {
			applyData(vt.start, vtType);
		}
		label(vt.start, vt.label, vt.cls.ns);
		return vtType;
	}

	/**
	 * The class struct at the namespace's own category path (/mg/mgType1/CPlayer for
	 * mg::mgType1::CPlayer). VariableUtilities.findOrCreateClassStruct matches by name
	 * only, so a global CPlayer would otherwise resolve to mg::mgType1::CPlayer's struct.
	 */
	static Structure classStruct(GhidraClass ns, DataTypeManager dtm) {
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

	private boolean addVfptr(ClassInfo c, VTable vt, DataType vtType) throws Exception {
		Structure s = classStruct(c.ns, dtm);
		int need = vt.offset + 4;
		if (s.isZeroLength() || s.getLength() < need) {
			s.growStructure(need - (s.isZeroLength() ? 0 : s.getLength()));
		}
		DataTypeComponent existing = s.getComponentContaining(vt.offset);
		if (existing != null && existing.getOffset() == vt.offset && existing.getLength() == 4 &&
			existing.getFieldName() != null && existing.getFieldName().startsWith("vftable")) {
			s.replaceAtOffset(vt.offset, new PointerDataType(vtType, dtm), 4, vt.label, null);
			return true;
		}
		for (int off = vt.offset; off < need; off++) {
			DataTypeComponent comp = s.getComponentContaining(off);
			if (comp != null && !Undefined.isUndefined(comp.getDataType())) {
				warnings.add(c.ns.getName(true) + " struct already has a field at offset " + off);
				return false;
			}
		}
		s.replaceAtOffset(vt.offset, new PointerDataType(vtType, dtm), 4, vt.label, null);
		return true;
	}

	// ---- export --------------------------------------------------------------------

	private void writeExports(File dir, Map<Address, ClassInfo> byTd, Map<Address, VTable> byCol,
			Map<Address, String> notes) throws Exception {
		String base = currentProgram.getName();
		try (PrintWriter out = new PrintWriter(new File(dir, base + ".rtti.tsv"))) {
			out.println("class\ttype_descriptor\tmangled\tchd_attributes\tvftables\tbases");
			for (ClassInfo c : byTd.values()) {
				List<String> vts = new ArrayList<>();
				for (VTable vt : c.vtables.values()) {
					vts.add(vt.offset + ":" + vt.start + ":" + vt.slots.size());
				}
				List<String> bases = new ArrayList<>();
				for (BaseDesc b : c.bases.subList(Math.min(1, c.bases.size()), c.bases.size())) {
					ClassInfo bc = byTd.get(b.td);
					String name = bc == null ? b.td.toString() : bc.ns.getName(true);
					bases.add(name + "@" + b.mdisp + (b.pdisp >= 0 ? "v" + b.pdisp + "," + b.vdisp : ""));
				}
				out.printf("%s\t%s\t%s\t%s\t%s\t%s%n", c.ns.getName(true), c.td, c.mangled,
					c.chd == null ? "" : Integer.toString(c.chdAttributes), String.join("|", vts),
					String.join("|", bases));
			}
		}
		try (PrintWriter out = new PrintWriter(new File(dir, base + ".vtables.tsv"))) {
			out.println("class\tvftable\tvfptr_offset\tslot\ttarget\tfunction\tnote");
			for (VTable vt : byCol.values()) {
				for (int i = 0; i < vt.slots.size(); i++) {
					Address t = vt.slots.get(i);
					Function f = getFunctionAt(t);
					out.printf("%s\t%s\t%d\t%d\t%s\t%s\t%s%n", vt.cls.ns.getName(true), vt.start, vt.offset, i, t,
						f == null ? "" : f.getName(true), notes.getOrDefault(t, "").trim());
				}
			}
		}
		try (PrintWriter out = new PrintWriter(new File(dir, base + ".vtable_refs.tsv"))) {
			out.println("class\tvftable\tref_from\tfunction");
			for (VTable vt : byCol.values()) {
				for (Reference r : getReferencesTo(vt.start)) {
					Function f = getFunctionContaining(r.getFromAddress());
					if (rdata.contains(r.getFromAddress())) {
						continue;
					}
					out.printf("%s\t%s\t%s\t%s%n", vt.cls.ns.getName(true), vt.start, r.getFromAddress(),
						f == null ? "" : f.getName(true));
				}
			}
		}
	}

	// ---- helpers ---------------------------------------------------------------------

	private void applyData(Address a, DataType dt) throws Exception {
		currentProgram.getListing().clearCodeUnits(a, a.add(dt.getLength() - 1), false);
		createData(a, dt);
	}

	private void label(Address a, String name, Namespace ns) throws Exception {
		Symbol s = symtab.getSymbol(name, a, ns);
		if (s == null) {
			s = symtab.createLabel(a, name, ns, SourceType.ANALYSIS);
		}
		s.setPrimary();
	}

	private String readCString(Address a) throws Exception {
		StringBuilder sb = new StringBuilder();
		for (byte b; (b = getByte(a)) != 0; a = a.add(1)) {
			sb.append((char) (b & 0xff));
		}
		return sb.toString();
	}

	private static String sanitize(String name) {
		return name.replace("::", "__").replaceAll("[^A-Za-z0-9_<>,]", "_");
	}
}
