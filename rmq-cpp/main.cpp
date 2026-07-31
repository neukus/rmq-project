#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>
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

struct SparseTableIdx {
    static std::string name() { return "SparseTable_Idx"; }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint32_t> table;  // argmin indices, level 0 dropped

    size_t n = 0;
    size_t levels = 0;

    static SparseTableIdx build(const std::vector<uint64_t>& data) {
        size_t n = data.size();

        if (n == 0) {
            return {&data, {}, 0, 0};
        }

        size_t levels = 32 - __builtin_clz((unsigned int)n);
        std::vector<uint32_t> table((levels > 1 ? levels - 1 : 0) * n);

        // level 1 (row 0): argmin over [j, j+1]
        for (size_t j = 0; j + 2 <= n; ++j) {
            table[j] = (data[j] <= data[j + 1]) ? (uint32_t)j : (uint32_t)(j + 1);
        }

        // level 2..levels-1
        for (size_t level = 2; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= n; ++j) {
                uint32_t a = table[(level - 2) * n + j];
                uint32_t b = table[(level - 2) * n + j + half];
                table[(level - 1) * n + j] = (data[a] <= data[b]) ? a : b;
            }
        }

        return {&data, std::move(table), n, levels};
    }

    size_t space() const { return sizeof(*this) + table.capacity() * sizeof(uint32_t); }

    uint64_t query(size_t l, size_t r) const {
        if (l == r)
            return (*data)[l];

        int level = 31 - __builtin_clz((unsigned int)(r - l + 1));
        size_t block_len = size_t(1) << level;
        size_t row = (size_t)level - 1;

        uint32_t a = table[row * n + l];
        uint32_t b = table[row * n + r - block_len + 1];
        return std::min((*data)[a], (*data)[b]);
    }
};

struct TwoNSegmentTree {
    static std::string name() { return "2N_SegTree"; }
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

struct NSegmentTree {
    static std::string name() { return "1N_SegTree"; }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint64_t> tree;
    size_t n = 0;

    static NSegmentTree build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        if (n == 0)
            return {&data, {}, 0};

        std::vector<uint64_t> tree(n);

        auto get_val = [&](size_t idx) { return idx < n ? tree[idx] : data[idx - n]; };

        for (size_t i = n - 1; i > 0; --i) {
            tree[i] = std::min(get_val(2 * i), get_val(2 * i + 1));
        }

        return {&data, std::move(tree), n};
    }

    inline uint64_t get_val(size_t idx) const { return idx < n ? tree[idx] : (*data)[idx - n]; }
    size_t space() const { return sizeof(*this) + (tree.capacity() * sizeof(uint64_t)); }

    uint64_t query(size_t l, size_t r) const {
        uint64_t res = UINT64_MAX;
        l += n;
        r += n + 1;

        while (l < r) {
            if (l & 1) {
                res = std::min(res, get_val(l++));
            }
            if (r & 1) {
                res = std::min(res, get_val(--r));
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
struct Fixed32BlockSize {
    static std::string name() { return "32"; }
    static size_t get(size_t n) { return 32; }
};
struct Fixed16BlockSize {
    static std::string name() { return "16"; }
    static size_t get(size_t n) { return 16; }
};
struct Fixed8BlockSize {
    static std::string name() { return "8"; }
    static size_t get(size_t n) { return 8; }
};
struct Fixed4BlockSize {
    static std::string name() { return "4"; }
    static size_t get(size_t n) { return 4; }
};

struct HalfLogBlockSize {
    static std::string name() { return "HalfLog"; }
    static size_t get(size_t n) {
        if (n < 2)
            return 1;
        size_t s = (32 - __builtin_clz((unsigned int)n)) / 2;  // (1/2) log2 n
        return s < 1 ? 1 : s;
    }
};

struct QuarterBlockSize {
    static std::string name() { return "QuarterLog"; }
    static size_t get(size_t n) {
        if (n < 2)
            return 1;
        size_t s = (32 - __builtin_clz((unsigned int)n)) / 4;  // (1/2) log2 n
        return s < 1 ? 1 : s;
    }
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
        size_t bl = l / block_size;
        size_t br = r / block_size;

        // case 1: same block
        if (bl == br) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix
        size_t first_block_end = std::min(n, (bl + 1) * block_size);
        uint64_t suf = (*data)[l];
        for (size_t i = l + 1; i < first_block_end; ++i) {
            suf = std::min(suf, (*data)[i]);
        }
        res = std::min(res, suf);

        // prefix
        size_t last_block_start = br * block_size;
        uint64_t prefix = (*data)[last_block_start];
        for (size_t i = last_block_start + 1; i <= r; ++i) {
            prefix = std::min(prefix, (*data)[i]);
        }
        res = std::min(res, prefix);

        // middle blocks (sparse table)
        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
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
struct BlocksIdx {
    static std::string name() { return "Blocks_Idx_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint32_t> blocks;  // sparse table of INDICES over block minima

    size_t n = 0;
    size_t block_size = 0;
    size_t num_blocks = 0;

    static BlocksIdx build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0)
            block_size = 1;
        size_t num_blocks = (n + block_size - 1) / block_size;

        // global argmin of each block
        std::vector<uint32_t> block_arg(num_blocks);
        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t end = std::min(n, start + block_size);
            size_t arg = start;
            for (size_t j = start + 1; j < end; ++j) {
                if (data[j] < data[arg])
                    arg = j;
            }
            block_arg[i] = (uint32_t)arg;
        }

        // sparse table over block minima, storing global indices
        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint32_t> table(levels * num_blocks);
        std::copy(block_arg.begin(), block_arg.end(), table.begin());
        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                uint32_t a = table[(level - 1) * num_blocks + j];
                uint32_t bb = table[(level - 1) * num_blocks + j + half];
                table[level * num_blocks + j] = (data[a] <= data[bb]) ? a : bb;
            }
        }

        return {&data, std::move(table), n, block_size, num_blocks};
    }

    size_t space() const { return sizeof(*this) + blocks.capacity() * sizeof(uint32_t); }

    uint64_t query(size_t l, size_t r) const {
        size_t bl = l / block_size;
        size_t br = r / block_size;

        // case 1: same block
        if (bl == br) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix (scanned on the fly)
        size_t first_block_end = std::min(n, (bl + 1) * block_size);
        uint64_t res = (*data)[l];
        for (size_t i = l + 1; i < first_block_end; ++i) {
            res = std::min(res, (*data)[i]);
        }

        // prefix (scanned on the fly)
        size_t last_block_start = br * block_size;
        for (size_t i = last_block_start; i <= r; ++i) {
            res = std::min(res, (*data)[i]);
        }

        // middle blocks (sparse table of indices)
        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;
            uint32_t a = blocks[level * num_blocks + mb_l];
            uint32_t b = blocks[level * num_blocks + mb_r - block_len + 1];
            res = std::min(res, std::min((*data)[a], (*data)[b]));
        }

        return res;
    }
};

template <typename SizePolicy>
struct BlocksPrecomputed {
    static std::string name() { return "Blocks_Precom_" + SizePolicy::name(); }
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
        size_t bl = l / block_size;
        size_t br = r / block_size;

        // case 1: same block
        if (bl == br) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix + prefix precomputed
        uint64_t res = std::min(suf[l], pre[r]);

        // middle blocks (sparse table)
        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
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
struct BlocksPrecomputedIdx {
    static std::string name() { return "Blocks_Precom_Idx_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint32_t> blocks;  // sparse table of INDICES
    std::vector<uint32_t> pre;     // pre[i] = argmin over [block_start(i), i]
    std::vector<uint32_t> suf;     // suf[i] = argmin over [i, block_end(i)]

    size_t n = 0;
    size_t block_size = 0;
    size_t num_blocks = 0;

    static BlocksPrecomputedIdx build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0)
            block_size = 1;
        size_t num_blocks = (n + block_size - 1) / block_size;

        std::vector<uint32_t> block_arg(num_blocks);
        std::vector<uint32_t> pre(n);
        std::vector<uint32_t> suf(n);

        // block argmin + prefix argmin
        for (size_t i = 0; i < n; ++i) {
            size_t b = i / block_size;
            size_t start = b * block_size;

            if (i == start) {
                pre[i] = (uint32_t)i;
                block_arg[b] = (uint32_t)i;
            } else {
                pre[i] = (data[i] < data[pre[i - 1]]) ? (uint32_t)i : pre[i - 1];
                if (data[i] < data[block_arg[b]])
                    block_arg[b] = (uint32_t)i;
            }
        }

        // suffix argmin (cast to int to prevent underflow)
        for (int i = (int)n - 1; i >= 0; --i) {
            size_t b = (size_t)i / block_size;
            size_t end = std::min(n, (b + 1) * block_size) - 1;

            if ((size_t)i == end) {
                suf[i] = (uint32_t)i;
            } else {
                suf[i] = (data[i] < data[suf[i + 1]]) ? (uint32_t)i : suf[i + 1];
            }
        }

        // sparse table over block minima, storing global indices
        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint32_t> table(levels * num_blocks);
        std::copy(block_arg.begin(), block_arg.end(), table.begin());
        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                uint32_t a = table[(level - 1) * num_blocks + j];
                uint32_t bb = table[(level - 1) * num_blocks + j + half];
                table[level * num_blocks + j] = (data[a] <= data[bb]) ? a : bb;
            }
        }
        return {&data, std::move(table), std::move(pre), std::move(suf), n, block_size, num_blocks};
    }
    size_t space() const {
        return sizeof(*this) + (blocks.capacity() + pre.capacity() + suf.capacity()) * sizeof(uint32_t);
    }
    uint64_t query(size_t l, size_t r) const {
        size_t bl = l / block_size;
        size_t br = r / block_size;
        // case 1: same block
        if (bl == br) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) {
                m = std::min(m, (*data)[i]);
            }
            return m;
        }

        // suffix + prefix
        uint64_t res = std::min((*data)[suf[l]], (*data)[pre[r]]);
        // middle blocks (sparse table of indices)
        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;
            uint32_t a = blocks[level * num_blocks + mb_l];
            uint32_t b = blocks[level * num_blocks + mb_r - block_len + 1];

            res = std::min(res, std::min((*data)[a], (*data)[b]));
        }
        return res;
    }
};

template <typename SizePolicy>
struct BlocksPrecomputedIdx16 {
    static std::string name() { return "Blocks_Precom_Idx16_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint32_t> blocks;
    std::vector<uint16_t> pre;  // offset within block of argmin over [start, i]
    std::vector<uint16_t> suf;  // offset within block of argmin over [i, end]

    size_t n = 0;
    size_t block_size = 0;
    size_t num_blocks = 0;

    static BlocksPrecomputedIdx16 build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0)
            block_size = 1;
        size_t num_blocks = (n + block_size - 1) / block_size;

        std::vector<uint32_t> block_arg(num_blocks);
        std::vector<uint16_t> pre(n);
        std::vector<uint16_t> suf(n);

        for (size_t i = 0; i < n; ++i) {
            size_t b = i / block_size;
            size_t start = b * block_size;

            if (i == start) {
                pre[i] = 0;
                block_arg[b] = (uint32_t)i;
            } else {
                size_t prev_global = start + pre[i - 1];
                pre[i] = (data[i] < data[prev_global]) ? (uint16_t)(i - start) : pre[i - 1];
                if (data[i] < data[block_arg[b]])
                    block_arg[b] = (uint32_t)i;
            }
        }

        for (int i = (int)n - 1; i >= 0; --i) {
            size_t b = (size_t)i / block_size;
            size_t start = b * block_size;
            size_t end = std::min(n, (b + 1) * block_size) - 1;

            if ((size_t)i == end) {
                suf[i] = (uint16_t)((size_t)i - start);
            } else {
                size_t next_global = start + suf[i + 1];
                suf[i] = (data[i] < data[next_global]) ? (uint16_t)((size_t)i - start) : suf[i + 1];
            }
        }

        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint32_t> table(levels * num_blocks);
        std::copy(block_arg.begin(), block_arg.end(), table.begin());
        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                uint32_t a = table[(level - 1) * num_blocks + j];
                uint32_t bb = table[(level - 1) * num_blocks + j + half];
                table[level * num_blocks + j] = (data[a] <= data[bb]) ? a : bb;
            }
        }
        return {&data, std::move(table), std::move(pre), std::move(suf), n, block_size, num_blocks};
    }
    size_t space() const {
        return sizeof(*this) + blocks.capacity() * sizeof(uint32_t) +
               (pre.capacity() + suf.capacity()) * sizeof(uint16_t);
    }
    uint64_t query(size_t l, size_t r) const {
        size_t bl = l / block_size;
        size_t br = r / block_size;

        if (bl == br) {
            uint64_t m = (*data)[l];
            for (size_t i = l + 1; i <= r; ++i) m = std::min(m, (*data)[i]);
            return m;
        }

        size_t ls = bl * block_size;
        size_t rs = br * block_size;
        uint64_t res = std::min((*data)[ls + suf[l]], (*data)[rs + pre[r]]);

        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;
            uint32_t a = blocks[level * num_blocks + mb_l];
            uint32_t b = blocks[level * num_blocks + mb_r - block_len + 1];
            res = std::min(res, std::min((*data)[a], (*data)[b]));
        }
        return res;
    }
};

template <typename SizePolicy>
struct CartesianBlocks {
    static std::string name() { return "Cartesian_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint64_t> table;
    std::vector<uint32_t> block_shape;
    std::vector<uint16_t> argmin;

    size_t n = 0;
    size_t bs = 0;
    size_t num_blocks = 0;
    size_t levels = 0;

    static CartesianBlocks build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0) {
            block_size = 1;
        }
        size_t num_blocks = (n + block_size - 1) / block_size;

        // (1) block minima
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

        // (2) sparse table over the block minima
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

        // (3) cartesian tree of each block + one arg-min table per distinct shape
        std::vector<uint32_t> shape(num_blocks);
        std::vector<uint16_t> argmin;
        std::unordered_map<uint64_t, uint32_t> id;  // shape bits -> index into argmin
        const size_t stride = block_size * block_size;
        for (size_t b = 0; b < num_blocks; ++b) {
            size_t start = b * block_size;
            size_t len = std::min(n, start + block_size) - start;

            // cartesian tree shape of this block
            // writing a 0 for every pop and a 1 for every push.
            uint64_t mask = 0;
            uint64_t stack[64];  // change from std::vector
            int top = 0;

            for (size_t i = start; i < len + start; ++i) {
                while (top > 0 && stack[top - 1] > data[i]) {
                    --top;
                    mask <<= 1;  // 0 for pop
                }
                stack[top++] = data[i];  // stack.push_back()
                mask = (mask << 1) | 1;  // 1 for push
            }

            auto [it, is_new] = id.try_emplace(mask, (uint32_t)(argmin.size() / stride));
            shape[b] = it->second;

            if (is_new) {  // -> precompute for this shape
                size_t base = argmin.size();
                argmin.resize(base + stride, 0);
                for (size_t i = 0; i < len; ++i) {
                    size_t arg = i;
                    for (size_t j = i; j < len; ++j) {
                        if (data[start + j] < data[start + arg])
                            arg = j;
                        argmin[base + i * block_size + j] = (uint16_t)arg;
                    }
                }
            }
        }

        return {&data, std::move(table), std::move(shape), std::move(argmin), n, block_size, num_blocks, levels};
    }

    size_t space() const {
        size_t total = sizeof(*this);
        total += table.capacity() * sizeof(uint64_t);
        total += block_shape.capacity() * sizeof(uint32_t);
        total += argmin.capacity() * sizeof(uint16_t);
        return total;
    }

    inline uint64_t in_block(size_t b, size_t i, size_t j) const {
        size_t arg = argmin[(size_t)block_shape[b] * bs * bs + i * bs + j];
        return (*data)[b * bs + arg];
    }

    uint64_t query(size_t l, size_t r) const {
        size_t bl = l / bs;
        size_t br = r / bs;

        // same block
        if (bl == br) {
            return in_block(bl, l - bl * bs, r - bl * bs);
        }

        // case 2: suffix of the left block + prefix of the right block
        // 2a: suffix of the left block
        uint64_t res = in_block(bl, l - bl * bs, std::min(n, (bl + 1) * bs) - 1 - bl * bs);

        // 2b: prefix
        res = std::min(res, in_block(br, 0, r - br * bs));

        // 2c: middle blocks (sparse table)
        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;

            uint64_t middle =
                std::min(table[level * num_blocks + mb_l], table[level * num_blocks + mb_r - block_len + 1]);
            res = std::min(res, middle);
        }

        return res;
    }
};

template <typename SizePolicy>
struct CartesianIdx {
    static std::string name() { return "Cartesian_Idx_" + SizePolicy::name(); }
    static size_t max_n() { return SIZE_MAX; }

    const std::vector<uint64_t>* data;
    std::vector<uint32_t> table;
    std::vector<uint32_t> block_shape;
    std::vector<uint8_t> argmin;  // flat: shape*(bs*bs) + i*bs + j

    size_t n = 0;
    size_t bs = 0;
    size_t num_blocks = 0;
    size_t levels = 0;

    static CartesianIdx build(const std::vector<uint64_t>& data) {
        size_t n = data.size();
        size_t block_size = SizePolicy::get(n);
        if (block_size == 0)
            block_size = 1;
        size_t num_blocks = (n + block_size - 1) / block_size;

        // (1) global index of each block minimum
        std::vector<uint32_t> block_arg(num_blocks);
        for (size_t i = 0; i < num_blocks; ++i) {
            size_t start = i * block_size;
            size_t end = std::min(n, start + block_size);
            size_t arg = start;
            for (size_t j = start + 1; j < end; ++j)
                if (data[j] < data[arg])
                    arg = j;
            block_arg[i] = (uint32_t)arg;
        }

        // (2) sparse table over block minima, storing global indices
        size_t levels = 32 - __builtin_clz((unsigned int)num_blocks);
        std::vector<uint32_t> table(levels * num_blocks);
        std::copy(block_arg.begin(), block_arg.end(), table.begin());
        for (size_t level = 1; level < levels; ++level) {
            size_t len = size_t(1) << level;
            size_t half = size_t(1) << (level - 1);
            for (size_t j = 0; j + len <= num_blocks; ++j) {
                uint32_t a = table[(level - 1) * num_blocks + j];
                uint32_t b = table[(level - 1) * num_blocks + j + half];
                table[level * num_blocks + j] = (data[a] <= data[b]) ? a : b;
            }
        }

        // (3) cartesian shapes + flat uint8 argmin
        std::vector<uint32_t> shape(num_blocks);
        std::vector<uint8_t> argmin;
        std::unordered_map<uint64_t, uint32_t> id;
        const size_t stride = block_size * block_size;
        for (size_t b = 0; b < num_blocks; ++b) {
            size_t start = b * block_size;
            size_t len = std::min(n, start + block_size) - start;

            uint64_t mask = 0;
            uint64_t stack[64];
            int top = 0;
            for (size_t i = start; i < len + start; ++i) {
                while (top > 0 && stack[top - 1] > data[i]) {
                    --top;
                    mask <<= 1;
                }
                stack[top++] = data[i];
                mask = (mask << 1) | 1;
            }
            auto [it, is_new] = id.try_emplace(mask, (uint32_t)(argmin.size() / stride));
            shape[b] = it->second;
            if (is_new) {
                size_t base = argmin.size();
                argmin.resize(base + stride, 0);
                for (size_t i = 0; i < len; ++i) {
                    size_t arg = i;
                    for (size_t j = i; j < len; ++j) {
                        if (data[start + j] < data[start + arg])
                            arg = j;
                        argmin[base + i * block_size + j] = (uint8_t)arg;
                    }
                }
            }
        }
        return {&data, std::move(table), std::move(shape), std::move(argmin), n, block_size, num_blocks, levels};
    }
    size_t space() const {
        size_t total = sizeof(*this);
        total += table.capacity() * sizeof(uint32_t);
        total += block_shape.capacity() * sizeof(uint32_t);
        total += argmin.capacity() * sizeof(uint8_t);
        return total;
    }
    inline uint64_t in_block(size_t b, size_t i, size_t j) const {
        size_t arg = argmin[(size_t)block_shape[b] * bs * bs + i * bs + j];
        return (*data)[b * bs + arg];
    }
    uint64_t query(size_t l, size_t r) const {
        size_t bl = l / bs;
        size_t br = r / bs;

        if (bl == br) {
            return in_block(bl, l - bl * bs, r - bl * bs);
        }

        uint64_t res = in_block(bl, l - bl * bs, std::min(n, (bl + 1) * bs) - 1 - bl * bs);
        res = std::min(res, in_block(br, 0, r - br * bs));

        if (bl + 1 < br) {
            size_t mb_l = bl + 1;
            size_t mb_r = br - 1;
            int level = 31 - __builtin_clz((unsigned int)(mb_r - mb_l + 1));
            size_t block_len = size_t(1) << level;
            uint32_t a = table[level * num_blocks + mb_l];
            uint32_t b = table[level * num_blocks + mb_r - block_len + 1];
            res = std::min(res, std::min((*data)[a], (*data)[b]));
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

        // bench<SparseTable>(input);
        bench<SparseTableIdx>(input);

        // bench<TwoNSegmentTree>(input);
        bench<NSegmentTree>(input);

        // bench<Blocks<SqrtBlockSize>>(input);
        // bench<Blocks<LogBlockSize>>(input);
        // bench<Blocks<HalfLogBlockSize>>(input);
        // bench<Blocks<Fixed64BlockSize>>(input);
        // bench<Blocks<Fixed32BlockSize>>(input);
        // bench<Blocks<Fixed16BlockSize>>(input);
        // bench<Blocks<Fixed8BlockSize>>(input);
        // bench<Blocks<Fixed4BlockSize>>(input);

        // bench<BlocksIdx<SqrtBlockSize>>(input);
        bench<BlocksIdx<LogBlockSize>>(input);  // <---------
        // bench<BlocksIdx<HalfLogBlockSize>>(input);
        // bench<BlocksIdx<Fixed64BlockSize>>(input);
        // bench<BlocksIdx<Fixed32BlockSize>>(input);
        // bench<BlocksIdx<Fixed16BlockSize>>(input);
        // bench<BlocksIdx<Fixed8BlockSize>>(input);
        // bench<BlocksIdx<Fixed4BlockSize>>(input);

        // bench<BlocksPrecomputed<SqrtBlockSize>>(input);
        // bench<BlocksPrecomputed<LogBlockSize>>(input);
        // bench<BlocksPrecomputed<HalfLogBlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed64BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed16BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed32BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed8BlockSize>>(input);
        // bench<BlocksPrecomputed<Fixed4BlockSize>>(input);

        // bench<BlocksPrecomputedIdx<SqrtBlockSize>>(input);
        // bench<BlocksPrecomputedIdx<LogBlockSize>>(input);
        // bench<BlocksPrecomputedIdx<HalfLogBlockSize>>(input);
        // bench<BlocksPrecomputedIdx<Fixed64BlockSize>>(input);
        // bench<BlocksPrecomputedIdx<Fixed16BlockSize>>(input);
        // bench<BlocksPrecomputedIdx<Fixed32BlockSize>>(input);
        // bench<BlocksPrecomputedIdx<Fixed8BlockSize>>(input);
        // bench<BlocksPrecomputedIdx<Fixed4BlockSize>>(input);

        bench<BlocksPrecomputedIdx16<SqrtBlockSize>>(input);  // <--------
        // bench<BlocksPrecomputedIdx16<LogBlockSize>>(input);
        // bench<BlocksPrecomputedIdx16<HalfLogBlockSize>>(input);

        // bench<CartesianBlocks<LogBlockSize>>(input);
        // bench<CartesianBlocks<HalfLogBlockSize>>(input);
        // bench<CartesianBlocks<QuarterBlockSize>>(input);
        // bench<CartesianBlocks<Fixed16BlockSize>>(input);
        // bench<CartesianBlocks<Fixed8BlockSize>>(input);
        // bench<CartesianBlocks<Fixed4BlockSize>>(input);

        // bench<CartesianIdx<LogBlockSize>>(input);
        // bench<CartesianIdx<HalfLogBlockSize>>(input);
        bench<CartesianIdx<QuarterBlockSize>>(input);  // <--------------
        // bench<CartesianIdx<Fixed16BlockSize>>(input);
        // bench<CartesianIdx<Fixed8BlockSize>>(input);
        // bench<CartesianIdx<Fixed4BlockSize>>(input);

        // TODO: Add other implementations here.
    }

    return 0;
}
