// Decompiles functions of the analyzed program into a text file, read-only.
// Arguments: <output file> <address> [<address> ...], addresses in hex; each one may be
// anywhere inside its function. Run through tools/ghidra/decompile.sh.
// When a pointer's struct type is smaller than the object (a base class's struct for a
// subclass object), Ghidra writes fields past its end as p[k].field_0xNN, counted from
// p + k * size. Each such access gets its offset from p as a comment: p[1].field_0x44/*+0x90*/.
//@category Ragexe

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.Pointer;
import ghidra.program.model.data.Structure;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Function;
import ghidra.program.model.pcode.HighFunction;
import ghidra.program.model.pcode.HighSymbol;

public class Decompile extends GhidraScript {
	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		DecompInterface decompiler = new DecompInterface();
		decompiler.openProgram(currentProgram);
		try (PrintWriter out = new PrintWriter(new File(args[0]), StandardCharsets.UTF_8)) {
			for (int k = 1; k < args.length; k++) {
				Function f = getFunctionContaining(toAddr(args[k]));
				if (f == null) {
					out.println("=== " + args[k] + " (no function)\n");
					continue;
				}
				DecompileResults r = decompiler.decompileFunction(f, 60, monitor);
				if (r.getDecompiledFunction() == null) {
					out.println("=== " + f.getName(true) + " @" + f.getEntryPoint() + " (decompile failed: "
						+ r.getErrorMessage() + ")\n");
					continue;
				}
				out.println("=== " + f.getName(true) + " @" + f.getEntryPoint());
				out.println(withOffsets(r.getDecompiledFunction().getC(), r.getHighFunction()));
			}
		}
		finally {
			decompiler.dispose();
		}
	}

	private static String withOffsets(String c, HighFunction hf) {
		if (hf == null) {
			return c;
		}
		Iterator<HighSymbol> symbols = hf.getLocalSymbolMap().getSymbols();
		while (symbols.hasNext()) {
			HighSymbol s = symbols.next();
			Structure st = pointedStruct(s.getDataType());
			if (st == null || st.getLength() <= 1) {
				continue;
			}
			Pattern p = Pattern.compile("\\b" + Pattern.quote(s.getName()) + "\\[(\\d+)\\]\\.(\\w+)");
			Matcher m = p.matcher(c);
			StringBuilder sb = new StringBuilder();
			while (m.find()) {
				long k = Long.parseLong(m.group(1));
				Long off = fieldOffset(st, m.group(2));
				String text = m.group(0);
				if (off != null && k > 0) {
					text += "/*+0x" + Long.toHexString(k * st.getLength() + off) + "*/";
				}
				m.appendReplacement(sb, Matcher.quoteReplacement(text));
			}
			m.appendTail(sb);
			c = sb.toString();
		}
		return c;
	}

	private static Structure pointedStruct(DataType t) {
		if (t instanceof TypeDef) {
			t = ((TypeDef) t).getBaseDataType();
		}
		if (!(t instanceof Pointer)) {
			return null;
		}
		DataType d = ((Pointer) t).getDataType();
		if (d instanceof TypeDef) {
			d = ((TypeDef) d).getBaseDataType();
		}
		return d instanceof Structure ? (Structure) d : null;
	}

	private static Long fieldOffset(Structure st, String name) {
		if (name.startsWith("field_0x")) {
			try {
				return Long.parseLong(name.substring(8), 16);
			}
			catch (NumberFormatException e) {
				return null;
			}
		}
		for (DataTypeComponent dc : st.getDefinedComponents()) {
			if (name.equals(dc.getFieldName())) {
				return (long) dc.getOffset();
			}
		}
		return null;
	}
}
