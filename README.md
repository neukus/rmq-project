# Range minimum queries programming project

Programming project along the Advanced Data Structures 2026 [KIT
course](https://ae.iti.kit.edu/english/5028.php).
See the [teaching page](https://curiouscoding.nl/teaching/) for the [RMQ script](https://curiouscoding.nl/teaching/rmq-notes/).

The project is to implement the following range minimum query (RMQ) algorithms:
- compute on the fly,
- precompute all queries,
- sparse array,
- segment tree,
- block based approach:
  - Vary the block size from e.g. 2 to 64.
  - Include both the variant that computes suffix/prefix minima on the fly, and
    the variant that precomputes them.
- Cartesian trees:
  - Again with varying block sizes.
  - Optional: Implement the recursive approaches that use $O(n)$ /bits/ or even
    $2n+o(n)$ bits.
 

Optimizing the code, both for speed and space usage, is encouraged but optional.

Then, write a report comparing the methods via 3 plots:
- how does *query time* scale with `n`?
- how does *space usage* scale with `n`?
- what is the space-time *trade-off* for e.g. `n=10^7`?
  - Optional: How does it change for larger `n`?

This repository already contains:
- A `justfile` and equivalent `makefile` to run everything.
- `input-generator` to generate the input files in `input/`
- A Rust sample program in `rmq-rust/src/main.rs`
- `plot.py` to make the plots
- `report/report.tex`: a starting point for the report

## Input data
Input data is provided in `input/{n}.in` as a plain text file in the following file format:

```
<n> <q>  # n and number of queries
a_1      # 1 <= n <= 10^8 values 
a_2      # 0 <= a_i < 2^64
...
a_n
s_1 e_1  # 0 <= q <= 10^7 query pairs
s_2 e_2  # 1 <= s_i <= t_i <= n
...
s_q e_q
```
For example:
```
4 2
10
5
8
4
1 3
3 4
```

You can generate it using `make generate-input` or `just generate-input`. This
requires a working Rust installation; see https://rustup.rs/.

The provided test cases contain both random data and queries, as well as edge
cases that your code should work on (e.g. increasing/decreasing data, only very
long query ranges, only very short query ranges, ...).

Note that for the final evaluation, we will build, test, and run your code
against 'private' test data to verify its correctness and to get speed
measurements for all submissions on the same hardware.

By default the input files are for `n` up to `10^7`, each with 2 million queries, but you
can also test with your own generated queries.

## Benchmarks
Your code should read the input, build the data structure, and /only then/ time how
long it takes to answer all queries. Then it should write the results (the
average time per query for each `n`) to standard out in CSV format.

The layout of `data.csv` is:

``` csv
n,q,name,space,sum,time
1000,2000000,Naive,16,88876699367779870,73.263246
3000,2000000,Naive,16,15097707945584911940,178.7236105
10000,2000000,Naive,16,15086364188337025273,558.876239
```
- `n`: the input size.
- `q`: the number of queries.
- `name`: the name of the RMQ implementation.
- `space`: the space in bytes that the data structure needs, *excluding* the
  input data.
- `sum`: the sum of the answers to all queries, to check correctness.
- `time`: the time _per query_ in nanoseconds.

## Plots
`plot.py` takes the data in `data.csv` and writes 3 plots `plots/{query_time,space,space_time_tradeoff}.png`.

Feel free to modify this to your liking, but please keep the output file names the same.

## Report
Submit a zip of this repo, excluding the `input` directory.
Submit a `abcd12.zip` file of this repo named after your student ID. It should contain:
- Your code. Update the `makefile` and/or `justfile` so that `build` builds your
  code. Remove templates for other languages.
- `/report.pdf`: a report (in English) containing:
  - *Introduction*: Briefly explains the files you submitted;
  - *Methods*: Briefly explains the algorithms you implemented and any
    optimizations you did. Specifically, explain the data structures (vectors,
    hash maps, ...) you used and what data they contain.

    State the asymptotic space and query time complexity of each method.
    Which methods are the fastest and smallest according to the theory?
  - *Results*: This is the focus of the report.
    - Contains (at least) the three plots.
    - *Analyse the results*: do the results match your expectations? If so, how
      closely? If not, why do they not match? 
    - Does the space usage of each method match the theory? Both asymptotically
      and numerically? Try to explain the observations.
    - Does the query time of each method match the theory? Again, both
      asymptotically and numerically? Try to explain the observations.
    - Which algorithms are Pareto-optimal? Which algorithms are best for small
      space? For speed? And what would you choose as general-purpose balanced
      tradeoff? Motivate your choice.
    - What parameters/block sizes are best?
    - How fast is the fastest method *in absolute ns/query*?
      Can you explain this number?
      Do you think this could be optimized further?
      What (hardware?) limit is the bottleneck?

## AI policy
Using AI is allowed (but discouraged) for the code.
Using AI is *not allowed* for the writing of the report.
