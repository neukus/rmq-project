import java.io.*;
import java.nio.file.*;
import java.util.*;

interface Rmq {
    String name();
    /** Space usage in bytes. */
    long space();
    /** Returns the minimum value in data[l..=r] (inclusive) as a raw long (unsigned). */
    long query(int l, int r);
}

class Input {
    final long[] data;
    final int[] queryL;
    final int[] queryR;

    Input(long[] data, int[] queryL, int[] queryR) {
        this.data = data;
        this.queryL = queryL;
        this.queryR = queryR;
    }
}

/** Trivial implementation that computes each query on the fly. */
class Naive implements Rmq {
    private final long[] data;

    private Naive(long[] data) { this.data = data; }

    static String staticName() { return "QuadraticQuery"; }
    // NOTE: Do not use this for the improved implementations!
    static int maxN() { return 10_000; }

    static Naive build(long[] data) { return new Naive(data); }

    @Override public String name() { return staticName(); }

    @Override public long space() {
        return (long) data.length * Long.BYTES;
    }

    @Override public long query(int l, int r) {
        long min = data[l];
        for (int i = l + 1; i <= r; i++)
            if (Long.compareUnsigned(data[i], min) < 0) min = data[i];
        return min;
    }
}

// -------------------------------------------------------------
// TODO: Implement the Rmq interface for additional data structures.
// -------------------------------------------------------------

class Main {

    /** Read the given input file, parsing all integers with full 64-bit precision. */
    static Input readInput(Path file) throws IOException {
        BufferedReader br = new BufferedReader(new FileReader(file.toFile()));
        StreamTokenizer st = new StreamTokenizer(br);
        st.ordinaryChars('0', '9');
        st.wordChars('0', '9');
        st.ordinaryChar('-');
        st.wordChars('-', '-');

        st.nextToken(); int n = Integer.parseInt(st.sval);
        st.nextToken(); int q = Integer.parseInt(st.sval);

        long[] data = new long[n];
        for (int i = 0; i < n; i++) { st.nextToken(); data[i] = Long.parseUnsignedLong(st.sval); }

        int[] queryL = new int[q], queryR = new int[q];
        for (int i = 0; i < q; i++) {
            st.nextToken(); queryL[i] = Integer.parseInt(st.sval);
            st.nextToken(); queryR[i] = Integer.parseInt(st.sval);
        }
        return new Input(data, queryL, queryR);
    }

    /** Bench a pre-built RMQ instance and print results in CSV format. */
    static void bench(Input input, String name, int maxN, Rmq rmq) {
        int n = input.data.length;
        System.err.printf("%10d\t%20s\t", n, name);
        if (n > maxN) { System.err.println("skipped"); return; }
        System.err.printf("%10d\t", rmq.space());
        int q = input.queryL.length;
        long start = System.nanoTime();
        long sum = 0;
        for (int i = 0; i < q; i++)
            sum += rmq.query(input.queryL[i], input.queryR[i]);
        double nsPerQuery = (double) (System.nanoTime() - start) / q;
        System.out.printf("%d,%d,%s,%d,%d,%.6f%n",
                n, q, name, rmq.space(), sum, nsPerQuery);
        System.err.printf("%3d\t%.2fns/q%n", sum % 1000, nsPerQuery);
    }

    public static void main(String[] args) throws IOException {
        if (args.length < 1) {
            System.err.println("Usage: rmq <input_dir>");
            System.exit(1);
        }
        System.out.println("n,q,name,space,sum,time");
        Path fileOrDir = Paths.get(args[0]);
        System.err.printf("Reading input from \"%s\" ..%n", fileOrDir);
        List<Input> inputs = new ArrayList<>();
        if (Files.isRegularFile(fileOrDir)) {
            inputs.add(readInput(fileOrDir));
        } else {
            try (DirectoryStream<Path> dir = Files.newDirectoryStream(fileOrDir, "*.in")) {
                for (Path entry : dir)
                    inputs.add(readInput(entry));
            }
            inputs.sort(Comparator.comparingInt(i -> i.data.length));
        }
        for (Input input : inputs) {
            bench(input, Naive.staticName(), Naive.maxN(), Naive.build(input.data));
            // TODO: Add other implementations here.
        }
    }
}
