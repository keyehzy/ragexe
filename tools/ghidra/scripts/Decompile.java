// Decompiles functions of the analyzed program into a text file, read-only.
// Arguments: <output file> <address> [<address> ...], addresses in hex; each one may be
// anywhere inside its function. Run through tools/ghidra/decompile.sh.
//@category Ragexe

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

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
				out.println(r.getDecompiledFunction().getC());
			}
		}
		finally {
			decompiler.dispose();
		}
	}
}
