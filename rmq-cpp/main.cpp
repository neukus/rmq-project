#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

// RMQ interface (duck-typed via templates):
//
//   static std::string name();
//   static size_t max_n();               // optional, defaults to SIZE_MAX
//   static RMQ build(const std::vector<uint64_t>& data);
//   size_t space() const;
//   uint64_t query(size_t l, size_t r) const;

// Trivial implementation that computes each query on the fly.
struct Naive {
    static std::string name() { return "QuadraticQuery"; }
    // NOTE: Improved implementations should simply return size_t::MAX.
    static size_t max_n() { return 10'000; }

    const std::vector<uint64_t>* data;

    static Naive build(const std::vector<uint64_t>& data) { return {&data}; }

    size_t space() const { return sizeof(*this); }

    uint64_t query(size_t l, size_t r) const {
        uint64_t min = (*data)[l];
        for (size_t i = l + 1; i <= r; ++i) min = std::min(min, (*data)[i]);
        return min;
    }
};

struct PrecomputeAll {
    static std::string name() { return "PrecomputeAllQueries"; }
    static size_t max_n() { return 10'000; }

    std::vector<uint64_t> table;
    size_t n = 0;

    static PrecomputeAll build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        std::vector<uint64_t> table(n * n);

        for (size_t l = 0; l < n; ++l) {
            uint64_t m = data[l];
            for (size_t r = l; r < n; ++r) {
                m = std::min(m, data[r]);
                table[l * n + r] = m;
            }
        }

        return {std::move(table), n};
    }

    size_t space() const { return sizeof(*this) + table.capacity() * sizeof(uint64_t); }

    uint64_t query(size_t l, size_t r) const { return table[l * n + r]; }
};

struct SparseTable {
    static std::string name() { return "SparseTable"; }
    static size_t max_n() { return SIZE_MAX; }

    // const std::vector<uint64_t>* data;
    std::vector<uint64_t> table;

    size_t n = 0;
    size_t levels = 0;

    static SparseTable build(const std::vector<uint64_t>& data) {
        size_t n = data.size();

        // safeguard for my wellbeing
        if (n == 0) {
            return {{}, 0, 0};
        }

        size_t levels = 32 - __builtin_clz((unsigned int)n);
        std::vector<uint64_t> table(levels * n);

        // level 0 fill in the base case :((
        std::copy(data.begin(), data.end(), table.begin());

        // level 1..levels
        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);

            for (size_t j = 0; j + len <= n; ++j) {
                table[level * n + j] = std::min(table[(level - 1) * n + j], table[(level - 1) * n + j + half]);
            }
        }

        return {std::move(table), n, levels};
    }

    size_t space() const { return sizeof(*this) + (table.capacity() * sizeof(uint64_t)); }

    uint64_t query(size_t l, size_t r) const {
        int level = 31 - __builtin_clz((unsigned int)(r - l + 1));
        size_t block_len = size_t(1) << level;

        return std::min(table[level * n + l], table[level * n + r - block_len + 1]);
    }
};

struct TwoNSegmentTree {
    static std::string name() { return "2N_SegmentTree"; }
    static size_t max_n() { return SIZE_MAX; }

    std::vector<uint64_t> tree;
    size_t n = 0;

    static TwoNSegmentTree build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        std::vector<uint64_t> tree(2 * n);

        // copy original data to leaves :(
        std::copy(data.begin(), data.end(), tree.begin() + n);

        for (size_t i = n - 1; i > 0; --i) {
            tree[i] = std::min(tree[2 * i], tree[2 * i + 1]);
        }

        return {std::move(tree), n};
    }

    size_t space() const { return sizeof(*this) + (tree.capacity() * sizeof(uint64_t)); }

    uint64_t query(size_t l, size_t r) const {
        uint64_t res = UINT64_MAX;
        l += n;
        r += n + 1;
        while (l < r) {
            if (l & 1) {
                res = std::min(res, tree[l++]);
            }
            if (r & 1) {
                res = std::min(res, tree[--r]);
            }
            l >>= 1;
            r >>= 1;
        }
        return res;
    }
};

struct SqrtBlockSize {
    static std::string name() { return "Sqrt"; }
    static size_t get(size_t n) { return std::sqrt(n); }
};

struct LogBlockSize {
    static std::string name() { return "Log"; }
    static size_t get(size_t n) { return 32 - __builtin_clz((unsigned int)n); }
};
struct Fixed64BlockSize {
    static std::string name() { return "64"; }
    static size_t get(size_t n) { return 64; }
};
struct Fixed8BlockSize {
    static std::string name() { return "8"; }
    static size_t get(size_t n) { return 8; }
};

struct Fixed15BlockSize {
    static std::string name() { return "15"; }
    static size_t get(size_t n) { return 15; }
};

template <typename SizePolicy>
struct Blocks {
    static std::string name() { return "Blocks_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint64_t> blocks;

    size_t n = 0;
    size_t block_size = 0;
    size_t num_blocks = 0;

    static Blocks build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0) {
            block_size = 1;
        }
        size_t num_blocks = (n + block_size - 1) / block_size;

        std::vector<uint64_t> block_min(num_blocks);
        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t end = std::min(n, start + block_size);
            uint64_t m = data[start];
            for (size_t j = start + 1; j < end; ++j) {
                m = std::min(m, data[j]);
            }
            block_min[i] = m;
        }

        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint64_t> table(levels * num_blocks);
        std::copy(block_min.begin(), block_min.end(), table.begin());

        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                table[level * num_blocks + j] =
                    std::min(table[(level - 1) * num_blocks + j], table[(level - 1) * num_blocks + j + half]);
            }
        }

        return {&data, std::move(table), n, block_size, num_blocks};
    }

    size_t space() const { return sizeof(*this) + blocks.capacity() * sizeof(uint64_t); }

    uint64_t query(size_t l, size_t r) const {
        uint64_t res = UINT64_MAX;
        size_t b_l = l / block_size;
        size_t b_r = r / block_size;

        // case 1: same block
        if (b_l == b_r) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix
        size_t first_block_end = std::min(n, (b_l + 1) * block_size);
        uint64_t suf = (*data)[l];
        for (size_t i = l + 1; i < first_block_end; ++i) {
            suf = std::min(suf, (*data)[i]);
        }
        res = std::min(res, suf);

        // prefix
        size_t last_block_start = b_r * block_size;
        uint64_t prefix = (*data)[last_block_start];
        for (size_t i = last_block_start + 1; i <= r; ++i) {
            prefix = std::min(prefix, (*data)[i]);
        }
        res = std::min(res, prefix);

        // middle blocks (sparse table)
        if (b_l + 1 < b_r) {
            size_t mb_l = b_l + 1;
            size_t mb_r = b_r - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;

            uint64_t middle =
                std::min(blocks[level * num_blocks + mb_l], blocks[level * num_blocks + mb_r - block_len + 1]);
            res = std::min(res, middle);
        }

        return res;
    }
};

template <typename SizePolicy>
struct BlocksPrecomputed {
    static std::string name() { return "Blocks_" + SizePolicy::name() + "_Precomputed"; }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint64_t> blocks;
    std::vector<uint64_t> pre;
    std::vector<uint64_t> suf;

    size_t n = 0;
    size_t block_size = 0;
    size_t num_blocks = 0;

    static BlocksPrecomputed build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0)
            block_size = 1;
        size_t num_blocks = (n + block_size - 1) / block_size;

        std::vector<uint64_t> block_min(num_blocks);
        std::vector<uint64_t> pre(n);
        std::vector<uint64_t> suf(n);

        // block min and prefix
        for (size_t i = 0; i < n; ++i) {
            size_t b = i / block_size;
            size_t start = b * block_size;

            if (i == start) {
                pre[i] = data[i];
                block_min[b] = data[i];
            } else {
                pre[i] = std::min(pre[i - 1], data[i]);
                block_min[b] = std::min(block_min[b], data[i]);
            }
        }

        // suffix
        // cast to int to prevent underflow
        for (int i = (int)n - 1; i >= 0; --i) {
            size_t b = i / block_size;
            size_t end = std::min(n, (b + 1) * block_size) - 1;

            if ((size_t)i == end) {
                suf[i] = data[i];
            } else {
                suf[i] = std::min(suf[i + 1], data[i]);
            }
        }

        // sparse table
        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint64_t> table(levels * num_blocks);
        std::copy(block_min.begin(), block_min.end(), table.begin());

        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                table[level * num_blocks + j] =
                    std::min(table[(level - 1) * num_blocks + j], table[(level - 1) * num_blocks + j + half]);
            }
        }

        return {&data, std::move(table), std::move(pre), std::move(suf), n, block_size, num_blocks};
    }

    size_t space() const {
        return sizeof(*this) + (blocks.capacity() + pre.capacity() + suf.capacity()) * sizeof(uint64_t);
    }

    uint64_t query(size_t l, size_t r) const {
        size_t b_l = l / block_size;
        size_t b_r = r / block_size;

        // case 1: same block
        if (b_l == b_r) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix + prefix precomputed
        uint64_t res = std::min(suf[l], pre[r]);

        // middle blocks (sparse table)
        if (b_l + 1 < b_r) {
            size_t mb_l = b_l + 1;
            size_t mb_r = b_r - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;

            uint64_t middle =
                std::min(blocks[level * num_blocks + mb_l], blocks[level * num_blocks + mb_r - block_len + 1]);
            res = std::min(res, middle);
        }

        return res;
    }
};

// -------------------------------------------------------------
// TODO: Implement the RMQ interface for additional data structures.
// -------------------------------------------------------------

struct Input {
    std::vector<uint64_t> data;
    std::vector<std::pair<size_t, size_t>> queries;
};

// Read the given input file.
Input read_input(const std::filesystem::path& file) {
    std::ifstream f(file);
    size_t n, q;
    f >> n >> q;
    Input input;
    input.data.resize(n);
    for (auto& v : input.data) f >> v;
    input.queries.resize(q);
    for (auto& [l, r] : input.queries) f >> l >> r;
    return input;
}

// Bench the given RMQ implementation on the given input, and print results in CSV format.
template <typename RMQ>
void bench(const Input& input) {
    std::cerr << std::setw(10) << input.data.size() << "\t" << std::setw(20) << RMQ::name() << "\t";

    size_t max_n = RMQ::max_n();

    if (input.data.size() > max_n) {
        std::cerr << "skipped\n";
        return;
    }

    auto rmq = RMQ::build(input.data);
    std::cerr << std::setw(10) << rmq.space() << "\t";

    auto start = std::chrono::high_resolution_clock::now();
    uint64_t sum = 0;
    for (auto& [l, r] : input.queries) sum += rmq.query(l, r);
    auto end = std::chrono::high_resolution_clock::now();

    double elapsed = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) /
                     static_cast<double>(input.queries.size());

    std::cout << input.data.size() << "," << input.queries.size() << "," << RMQ::name() << "," << rmq.space() << ","
              << sum << "," << elapsed << "\n";
    std::cerr << std::setw(3) << (sum % 1000) << "\t" << std::fixed << std::setprecision(2) << elapsed << "ns/q\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rmq-cpp <input_dir>\n";
        return 1;
    }

    std::cout << "n,q,name,space,sum,time\n";

    std::filesystem::path file_or_dir(argv[1]);
    std::cerr << "Reading input from " << file_or_dir << " ..\n";

    std::vector<Input> inputs;
    if (std::filesystem::is_regular_file(file_or_dir)) {
        inputs.push_back(read_input(file_or_dir));
    } else {
        for (auto& entry : std::filesystem::directory_iterator(file_or_dir)) {
            if (entry.path().extension() == ".in")
                inputs.push_back(read_input(entry.path()));
        }
        std::sort(inputs.begin(), inputs.end(),
                  [](const Input& a, const Input& b) { return a.data.size() < b.data.size(); });
    }

    for (const auto& input : inputs) {
        bench<Naive>(input);
        bench<PrecomputeAll>(input);
        bench<SparseTable>(input);
        bench<TwoNSegmentTree>(input);
        bench<Blocks<SqrtBlockSize>>(input);
        bench<Blocks<LogBlockSize>>(input);
        // bench<Blocks<Fixed64BlockSize>>(input);
        // bench<Blocks<Fixed15BlockSize>>(input);
        // bench<Blocks<Fixed8BlockSize>>(input);
        bench<BlocksPrecomputed<SqrtBlockSize>>(input);
        bench<BlocksPrecomputed<LogBlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed64BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed15BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed8BlockSize>>(input);
        // TODO: Add other implementations here.
    }

    return 0;
}
