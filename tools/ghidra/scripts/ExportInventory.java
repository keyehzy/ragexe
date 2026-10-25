// Exports the function inventory and analysis summary used to seed the ledger.
// Arguments: <output directory>
// Writes <program>.functions.tsv, <program>.classes.tsv and <program>.summary.txt.
//@category Ragexe

import java.io.File;
import java.io.PrintWriter;
import java.util.Map;
import java.util.TreeMap;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Bookmark;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.GhidraClass;
import ghidra.program.model.symbol.Namespace;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;

public class ExportInventory extends GhidraScript {

	@Override
	protected void run() throws Exception {
		String[] args = getScriptArgs();
		File outDir = new File(args.length > 0 ? args[0] : ".");
		outDir.mkdirs();
		String base = currentProgram.getName();

		long totalBytes = 0, fidBytes = 0, thunkCount = 0, fidCount = 0, namedCount = 0;
		Map<String, Integer> bySource = new TreeMap<>();

		try (PrintWriter out = new PrintWriter(new File(outDir, base + ".functions.tsv"))) {
			out.println("entry\tsize\tnamespace\tname\tsource\tfid\tthunk\tcalling_convention\tsignature");
			FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
			while (it.hasNext() && !monitor.isCancelled()) {
				Function f = it.next();
				long size = f.getBody().getNumAddresses();
				boolean fid = isFidMatch(f.getEntryPoint());
				SourceType src = f.getSymbol().getSource();
				bySource.merge(src.toString(), 1, Integer::sum);
				totalBytes += size;
				if (fid) {
					fidCount++;
					fidBytes += size;
				}
				if (f.isThunk()) {
					thunkCount++;
				}
				if (src != SourceType.DEFAULT) {
					namedCount++;
				}
				out.printf("%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s%n",
					f.getEntryPoint(), size, f.getParentNamespace().getName(true), f.getName(),
					src, fid ? "1" : "0", f.isThunk() ? "1" : "0",
					f.getCallingConventionName(), f.getPrototypeString(false, false));
			}
		}

		int classCount = 0;
		SymbolTable st = currentProgram.getSymbolTable();
		try (PrintWriter out = new PrintWriter(new File(outDir, base + ".classes.tsv"))) {
			out.println("class\tparent_namespace\tfunctions\tvftables");
			for (GhidraClass c : iterable(st.getClassNamespaces())) {
				classCount++;
				int functions = 0, vftables = 0;
				SymbolIterator children = st.getSymbols(c);
				while (children.hasNext()) {
					Symbol s = children.next();
					if (s.getObject() instanceof Function) {
						functions++;
					}
					else if (s.getName().startsWith("vftable")) {
						vftables++;
					}
				}
				Namespace parent = c.getParentNamespace();
				out.printf("%s\t%s\t%d\t%d%n", c.getName(), parent.getName(true), functions, vftables);
			}
		}

		long functionCount = 0;
		for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
			functionCount++;
		}
		long externalCount = 0;
		for (Function f : currentProgram.getFunctionManager().getExternalFunctions()) {
			externalCount++;
		}
		try (PrintWriter out = new PrintWriter(new File(outDir, base + ".summary.txt"))) {
			out.printf("program            %s%n", base);
			out.printf("language           %s / %s%n", currentProgram.getLanguageID(),
				currentProgram.getCompilerSpec().getCompilerSpecID());
			out.printf("functions          %d%n", functionCount);
			out.printf("external functions %d%n", externalCount);
			out.printf("function bytes     %d%n", totalBytes);
			out.printf("thunks             %d%n", thunkCount);
			out.printf("non-default names  %d%n", namedCount);
			out.printf("FID matches        %d (%d bytes, %.1f%% of function bytes)%n", fidCount, fidBytes,
				totalBytes == 0 ? 0.0 : 100.0 * fidBytes / totalBytes);
			out.printf("class namespaces   %d%n", classCount);
			for (Map.Entry<String, Integer> e : bySource.entrySet()) {
				out.printf("source %-11s %d%n", e.getKey(), e.getValue());
			}
		}
		println("Inventory written to " + outDir.getAbsolutePath());
	}

	private boolean isFidMatch(Address entry) {
		for (Bookmark b : currentProgram.getBookmarkManager().getBookmarks(entry)) {
			if (b.getCategory().startsWith("Function ID Analyzer")) {
				return true;
			}
		}
		return false;
	}

	private static <T> Iterable<T> iterable(java.util.Iterator<T> it) {
		return () -> it;
	}
}
