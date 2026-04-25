// connect4_clean.cpp - Perfect Connect Four AI for WebAssembly
// Based on connect_four.cpp - AI logic kept EXACTLY the same as PC version
// Only PC-specific code (windows.h, console I/O) removed
// Compile: emcc connect4_clean.cpp -O3 -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s TOTAL_MEMORY=2147483648 -s EXPORTED_FUNCTIONS=_init_ai,_load_book,_load_cache,_get_best_move,_reset_game,_play_move,_get_cell,_is_game_over,_get_winner -s EXPORTED_RUNTIME_METHODS=ccall,cwrap -o connect4_clean.js

#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <string>
#include <climits>
#include <unordered_map>

// board dimensions
static constexpr int ROWS = 6;
static constexpr int COLS = 7;
static constexpr int H    = ROWS + 1;

// bitboard helpers
static inline uint64_t bot(int c)   { return 1ULL << (c * H); }
static inline uint64_t top(int c)   { return 1ULL << ((ROWS-1) + c*H); }
static inline uint64_t cmask(int c) { return ((1ULL<<ROWS)-1ULL) << (c*H); }

static uint64_t BOT_ROW = 0, FULL = 0;
static void init_masks() {
    for (int c = 0; c < COLS; c++) { BOT_ROW |= bot(c); FULL |= cmask(c); }
}

static const int ORDER[] = {3, 2, 4, 1, 5, 0, 6};

static bool has_won(uint64_t p) {
    uint64_t m;
    m = p & (p >> H);     if (m & (m >> (2*H)))     return true;
    m = p & (p >> (H+1)); if (m & (m >> (2*(H+1)))) return true;
    m = p & (p >> (H-1)); if (m & (m >> (2*(H-1)))) return true;
    m = p & (p >> 1);     if (m & (m >> 2))          return true;
    return false;
}

static uint64_t win_pos(uint64_t pos, uint64_t mask) {
    uint64_t r = 0, p;
    r |= (pos << 1) & (pos << 2) & (pos << 3);
    p=(pos>>H)&(pos>>(2*H));  r|=p&(pos>>(3*H)); r|=p&(pos<<H);
    p=(pos<<H)&(pos<<(2*H));  r|=p&(pos<<(3*H)); r|=p&(pos>>H);
    p=(pos>>(H-1))&(pos>>(2*(H-1))); r|=p&(pos>>(3*(H-1))); r|=p&(pos<<(H-1));
    p=(pos<<(H-1))&(pos<<(2*(H-1))); r|=p&(pos<<(3*(H-1))); r|=p&(pos>>(H-1));
    p=(pos>>(H+1))&(pos>>(2*(H+1))); r|=p&(pos>>(3*(H+1))); r|=p&(pos<<(H+1));
    p=(pos<<(H+1))&(pos<<(2*(H+1))); r|=p&(pos<<(3*(H+1))); r|=p&(pos>>(H+1));
    return r & (FULL ^ mask);
}

static inline uint64_t possible(uint64_t mask)        { return (mask + BOT_ROW) & FULL; }
static inline bool     can_play(uint64_t mask, int c) { return !(mask & top(c)); }
static inline uint64_t mirror_bits(uint64_t b) {
    uint64_t r = 0;
    constexpr uint64_t COL_BITS = (1ULL << H) - 1ULL;
    for (int c = 0; c < COLS; c++) {
        uint64_t col = (b >> (c * H)) & COL_BITS;
        r |= col << ((COLS - 1 - c) * H);
    }
    return r;
}
static inline uint64_t tt_key(uint64_t pos, uint64_t mask) {
    uint64_t k = pos + mask;
    uint64_t km = mirror_bits(pos) + mirror_bits(mask);
    return (km < k) ? km : k;
}
static inline uint64_t possible_non_losing(uint64_t pos, uint64_t mask) {
    uint64_t pm = possible(mask);
    uint64_t ow = win_pos(mask ^ pos, mask);
    uint64_t forced = pm & ow;
    if (forced) {
        if (forced & (forced - 1)) return 0;
        pm = forced;
    }
    return pm & ~(ow >> 1);
}

// transposition table - SAME SIZE AS PC VERSION (2 GiB)
struct TT {
    struct Entry { uint32_t key; int8_t lb, ub; };
    static constexpr size_t ENTRY_BYTES = sizeof(Entry);
    static constexpr size_t HASH_BITS   = 28;
    static constexpr size_t N           = size_t(1) << HASH_BITS;
    static constexpr size_t TABLE_BYTES = N * ENTRY_BYTES;  // ~2 GiB
    std::vector<Entry> t;
    TT() : t(N) { clear(); }
    void clear() { memset(t.data(), 0, t.size() * sizeof(Entry)); }
    bool get(uint64_t k, int &lb, int &ub) const {
        const Entry &e = t[k & (N-1)];
        if (e.key == uint32_t(k >> 24)) { lb = e.lb; ub = e.ub; return true; }
        return false;
    }
    void put(uint64_t k, int lb, int ub) {
        Entry &e = t[k & (N-1)];
        e.key = uint32_t(k >> 24);
        e.lb  = int8_t(lb);
        e.ub  = int8_t(ub);
    }
} g_tt;

// Killer moves and history - SAME AS PC VERSION
static int killer_moves[43][2];
static int history_score[COLS];
void init_killers() { memset(killer_moves, -1, sizeof(killer_moves)); }
void init_history() { memset(history_score, 0, sizeof(history_score)); }

// key3 for opening book - SAME AS PC VERSION
static uint64_t key3(uint64_t pos, uint64_t mask) {
    auto partialKey3 = [&](uint64_t &key, int col) {
        for (uint64_t bit = 1ULL << (col * H); bit & mask; bit <<= 1) {
            key *= 3;
            if (bit & pos) key += 1;
            else           key += 2;
        }
        key *= 3;
    };
    uint64_t kf = 0;
    for (int i = 0;        i < COLS; i++) partialKey3(kf, i);
    uint64_t kr = 0;
    for (int i = COLS - 1; i >= 0;   i--) partialKey3(kr, i);
    return (kf < kr ? kf : kr) / 3;
}

// Opening book - SAME AS PC VERSION
struct BookProbe {
    bool hit = false;
    size_t idx = 0;
    uint64_t partial = 0;
    uint64_t stored = 0;
    uint8_t raw = 0;
    int score = INT_MIN;
};

struct Book {
    std::vector<uint8_t> keys;
    std::vector<uint8_t> vals;
    size_t sz        = 0;
    int    depth     = 0;
    int    key_bytes = 0;
    int    val_bytes = 0;
    int    log_size  = 0;
    bool   ok        = false;

    static bool is_prime(size_t n) {
        if (n < 2) return false;
        if ((n % 2) == 0) return n == 2;
        for (size_t d = 3; d * d <= n; d += 2) {
            if ((n % d) == 0) return false;
        }
        return true;
    }

    static size_t next_prime(size_t n) {
        if (n <= 2) return 2;
        if ((n % 2) == 0) n++;
        while (!is_prime(n)) n += 2;
        return n;
    }

    void reset() {
        keys.clear();
        vals.clear();
        sz = 0;
        depth = 0;
        key_bytes = 0;
        val_bytes = 0;
        log_size = 0;
        ok = false;
    }

    bool load(const std::string &path) {
        reset();
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;

        uint8_t hdr[6] = {};
        f.read((char*)hdr, 6);
        if (!f) return false;

        int w = hdr[0];
        int h = hdr[1];
        depth = hdr[2];
        key_bytes = hdr[3];
        val_bytes = hdr[4];
        log_size = hdr[5];

        if (w != COLS || h != ROWS) return false;
        if (key_bytes < 1 || key_bytes > 8) return false;
        if (val_bytes != 1) return false;
        if (log_size < 1 || log_size > 40) return false;

        sz = next_prime(size_t(1) << log_size);

        f.seekg(0, std::ios::end);
        long long file_sz = (long long)f.tellg();
        long long expected_sz = 6LL + (long long)sz * key_bytes + (long long)sz * val_bytes;
        if (file_sz != expected_sz) {
            reset();
            return false;
        }

        keys.resize(sz * key_bytes);
        vals.resize(sz);
        f.seekg(6, std::ios::beg);
        f.read((char*)keys.data(), (std::streamsize)(sz * key_bytes));
        f.read((char*)vals.data(), (std::streamsize)sz);

        ok = f.good();
        if (!ok) reset();
        return ok;
    }

    bool load_from_memory(const uint8_t* data, size_t size) {
        reset();
        if (size < 6) return false;

        const uint8_t* hdr = data;
        int w = hdr[0];
        int h = hdr[1];
        depth = hdr[2];
        key_bytes = hdr[3];
        val_bytes = hdr[4];
        log_size = hdr[5];

        if (w != COLS || h != ROWS) return false;
        if (key_bytes < 1 || key_bytes > 8) return false;
        if (val_bytes != 1) return false;
        if (log_size < 1 || log_size > 40) return false;

        sz = next_prime(size_t(1) << log_size);

        size_t expected_sz = 6 + sz * key_bytes + sz * val_bytes;
        if (size != expected_sz) {
            reset();
            return false;
        }

        keys.resize(sz * key_bytes);
        vals.resize(sz);
        memcpy(keys.data(), data + 6, sz * key_bytes);
        memcpy(vals.data(), data + 6 + sz * key_bytes, sz);

        ok = true;
        return ok;
    }

    BookProbe probe(uint64_t k3, int moves) const {
        BookProbe p;
        if (!ok || moves > depth || sz == 0) return p;

        p.idx = (size_t)(k3 % sz);

        if (key_bytes == 8) p.partial = k3;
        else p.partial = k3 & ((1ULL << (key_bytes * 8)) - 1ULL);

        if (p.partial == 0) return p;

        memcpy(&p.stored, keys.data() + p.idx * key_bytes, key_bytes);
        if (p.stored != p.partial) return p;

        p.raw = vals[p.idx];
        if (p.raw == 0) return p;

        p.hit = true;
        p.score = (int)p.raw - 19;
        return p;
    }

    int get(uint64_t k3, int moves) const {
        return probe(k3, moves).score;
    }
} g_book;

// Persistent cache - SAME AS PC VERSION
static std::unordered_map<uint64_t, int> g_opening_override;
static bool g_persist_dirty = false;
static std::string g_persist_path = "opening_override.cache";
static constexpr size_t PERSIST_MAX_ENTRIES = 2'000'000;

static bool load_persistent_override(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    uint32_t magic = 0;
    uint32_t version = 0;
    uint64_t count = 0;
    f.read((char*)&magic, sizeof(magic));
    f.read((char*)&version, sizeof(version));
    f.read((char*)&count, sizeof(count));
    if (!f) return false;
    if (magic != 0x34434643u || version != 1u) return false; // "CFC4"
    if (count > PERSIST_MAX_ENTRIES) return false;
    g_opening_override.clear();
    if (count > 1000) g_opening_override.reserve((size_t)count);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t k = 0;
        int8_t v = 0;
        f.read((char*)&k, sizeof(k));
        f.read((char*)&v, sizeof(v));
        if (!f) return false;
        g_opening_override[k] = (int)v;
    }
    g_persist_dirty = false;
    return true;
}

static bool load_persistent_override_from_memory(const uint8_t* data, size_t size) {
    if (size < 16) return false; // magic(4) + version(4) + count(8)
    
    const uint8_t* p = data;
    uint32_t magic = *(uint32_t*)p; p += 4;
    uint32_t version = *(uint32_t*)p; p += 4;
    uint64_t count = *(uint64_t*)p; p += 8;
    
    if (magic != 0x34434643u || version != 1u) return false;
    if (count > PERSIST_MAX_ENTRIES) return false;
    
    size_t expected_size = 16 + count * 9; // 16 header + 9 bytes per entry (8 key + 1 value)
    if (size != expected_size) return false;
    
    g_opening_override.clear();
    if (count > 1000) g_opening_override.reserve((size_t)count);
    
    for (uint64_t i = 0; i < count; i++) {
        uint64_t k = *(uint64_t*)p; p += 8;
        int8_t v = *(int8_t*)p; p += 1;
        g_opening_override[k] = (int)v;
    }
    
    g_persist_dirty = false;
    return true;
}

static bool save_persistent_override(const std::string &path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const uint32_t magic = 0x34434643u;
    const uint32_t version = 1u;
    const uint64_t count = (uint64_t)g_opening_override.size();
    f.write((const char*)&magic, sizeof(magic));
    f.write((const char*)&version, sizeof(version));
    f.write((const char*)&count, sizeof(count));
    for (const auto &kv : g_opening_override) {
        uint64_t k = kv.first;
        int vv = kv.second;
        if (vv < -64) vv = -64;
        if (vv > 63) vv = 63;
        int8_t v = (int8_t)vv;
        f.write((const char*)&k, sizeof(k));
        f.write((const char*)&v, sizeof(v));
    }
    bool ok = f.good();
    if (ok) g_persist_dirty = false;
    return ok;
}

// negamax solver - EXACTLY SAME AS PC VERSION
static int negamax(uint64_t pos, uint64_t mask, int moves, int alpha, int beta, int max_depth = 42) {
    uint64_t w = win_pos(pos, mask);
    if (possible(mask) & w) return (ROWS*COLS+1-moves)/2;
    if (moves == ROWS*COLS) return 0;
    if (moves >= max_depth) return 0;
    uint64_t non_losing = possible_non_losing(pos, mask);
    if (!non_losing) return -(ROWS*COLS-moves)/2;

    int max_score = (ROWS * COLS - 1 - moves) / 2;
    if (beta > max_score) { beta = max_score; if (alpha >= beta) return beta; }
    int min_score = -((ROWS * COLS - 2 - moves) / 2);
    if (alpha < min_score) { alpha = min_score; if (alpha >= beta) return alpha; }
    uint64_t k = tt_key(pos, mask);
    {
        int lb, ub;
        if (g_tt.get(k, lb, ub)) {
            if (lb >= beta)  return lb;
            if (ub <= alpha) return ub;
            if (lb > alpha)  alpha = lb;
            if (ub < beta)   beta  = ub;
        }
    }
    int orig_alpha = alpha;
    struct MoveHint { int col; int hint; } move_list[COLS];
    int nc = 0;
    int d = moves % 43;
    for (int c : ORDER) {
        if (!(non_losing & cmask(c))) continue;
        int hint = history_score[c];
        if (c == killer_moves[d][0]) hint = 50;
        else if (c == killer_moves[d][1]) hint = 40;
        move_list[nc++] = {c, hint};
    }
    for (int i = 1; i < nc; i++) {
        MoveHint key = move_list[i];
        int j = i - 1;
        while (j >= 0 && move_list[j].hint < key.hint) {
            move_list[j+1] = move_list[j];
            j--;
        }
        move_list[j+1] = key;
    }
    int best = -(ROWS*COLS);
    for (int i = 0; i < nc; i++) {
        int c = move_list[i].col;
        uint64_t np = (mask+bot(c)) & cmask(c);
        uint64_t npos = mask ^ pos;
        uint64_t nmask = mask | np;
        int s;
        if (i == 0 || alpha + 1 >= beta) {
            s = -negamax(npos, nmask, moves + 1, -beta, -alpha, max_depth);
        } else {
            s = -negamax(npos, nmask, moves + 1, -alpha - 1, -alpha, max_depth);
            if (s > alpha && s < beta)
                s = -negamax(npos, nmask, moves + 1, -beta, -alpha, max_depth);
        }
        if (s > best) best = s;
        if (s > alpha) alpha = s;
        if (alpha >= beta) {
            int d = moves % 43;
            if (c != killer_moves[d][0]) {
                killer_moves[d][1] = killer_moves[d][0];
                killer_moves[d][0] = c;
            }
            history_score[c] += (moves + 1) * (moves + 1);
            break;
        }
    }
    if (best <= orig_alpha) g_tt.put(k, -(ROWS*COLS), best);
    else if (best >= beta)  g_tt.put(k, best, ROWS*COLS);
    else                    g_tt.put(k, best, best);
    return best;
}

// get_best_move - EXACTLY SAME AS PC VERSION
static int get_best_move(uint64_t pos, uint64_t mask, int moves) {
    uint64_t w = win_pos(pos, mask), cw = possible(mask) & w;
    if (cw) for (int c : ORDER) if (cw & cmask(c)) return c;

    uint64_t opp = mask^pos, ow = win_pos(opp, mask);
    uint64_t forced = possible(mask) & ow;
    if (forced && !(forced & (forced-1)))
        for (int c : ORDER) if (forced & cmask(c)) return c;

    if (moves == 0) return 3;

    int scores[COLS]; bool valid[COLS] = {};
    int best_score = INT_MIN;
    int best_book_score = INT_MIN;
    int best_book_col = -1;
    int missing[COLS]; int miss_count = 0;

    for (int c : ORDER) {
        if (!can_play(mask, c)) continue;
        uint64_t np  = (mask + bot(c)) & cmask(c);
        uint64_t nm  = mask | np;
        uint64_t nps = mask ^ pos;
        uint64_t ck3 = key3(nps, nm);
        auto override_it = g_opening_override.find(ck3);
        if (override_it != g_opening_override.end()) {
            scores[c] = override_it->second;
            if (scores[c] > best_book_score) {
                best_book_score = scores[c];
                best_book_col = c;
            }
            if (scores[c] > best_score) best_score = scores[c];
        } else {
            BookProbe probe = g_book.probe(ck3, moves + 1);
            if (probe.hit) {
                scores[c] = -probe.score;
                if (scores[c] > best_book_score) {
                    best_book_score = scores[c];
                    best_book_col = c;
                }
                if (scores[c] > best_score) best_score = scores[c];
            } else {
                scores[c] = INT_MIN;
                missing[miss_count++] = c;
            }
        }
        valid[c] = true;
    }
    if (miss_count == 0) {
        for (int c : ORDER) if (valid[c] && scores[c] == best_score) return c;
    }

    if (best_book_score > 0 && best_book_col >= 0) {
        for (int c : ORDER) if (valid[c] && scores[c] == best_book_score) return c;
    }

    int remaining = ROWS * COLS - moves;
    int max_depth = moves + remaining;
    int max_theoretical = (ROWS * COLS - 1 - moves) / 2;

    if (best_score == max_theoretical) {
        for (int c : ORDER) if (valid[c] && scores[c] == best_score) return c;
    }

    int alpha = (best_score == INT_MIN) ? -(ROWS * COLS) : best_score - 2;
    int beta = (best_score == INT_MIN) ? max_theoretical : best_score + 2;
    if (alpha < -(ROWS * COLS)) alpha = -(ROWS * COLS);
    if (beta > max_theoretical) beta = max_theoretical;
    if (alpha >= beta) {
        for (int c : ORDER) if (valid[c] && scores[c] == best_score) return c;
    }

    int ordered_missing[COLS];
    int ordered_count = 0;
    for (int i = 0; i < miss_count; i++) {
        int c = missing[i];
        if (scores[c] != INT_MIN) ordered_missing[ordered_count++] = c;
    }
    for (int i = 0; i < miss_count; i++) {
        int c = missing[i];
        if (scores[c] == INT_MIN) ordered_missing[ordered_count++] = c;
    }

    for (int i = 0; i < ordered_count; i++) {
        int c = ordered_missing[i];
        uint64_t np = (mask + bot(c)) & cmask(c);
        uint64_t nm = mask | np;
        uint64_t nps = mask ^ pos;
        int s = -negamax(nps, nm, moves + 1, -alpha - 1, -alpha, max_depth);
        if (s > alpha) {
            s = -negamax(nps, nm, moves + 1, -beta, -alpha, max_depth);
        }
        scores[c] = s;
        if (moves + 1 <= g_book.depth + 10 && g_opening_override.size() < PERSIST_MAX_ENTRIES) {
            g_opening_override[key3(nps, nm)] = s;
            g_persist_dirty = true;
        }
        if (s > best_score) best_score = s;
        if (s > alpha) alpha = s;
        if (alpha >= beta) break;
    }

    for (int c : ORDER) if (valid[c] && scores[c] == best_score) return c;
    return 3;
}

// Game state - SAME AS PC VERSION
struct Game {
    uint64_t pos1 = 0, pos2 = 0, mask = 0;
    int moves = 0;
    bool game_over = false;
    int winner = 0; // 0 = none, 1 = player1, 2 = player2, 3 = draw
    
    bool p1_turn() const { return moves % 2 == 0; }
    uint64_t cur_pos() const { return p1_turn() ? pos1 : pos2; }
    bool play(int col) {
        uint64_t &cur = p1_turn() ? pos1 : pos2;
        uint64_t np = (mask + bot(col)) & cmask(col);
        cur  |= np;
        mask |= np;
        moves++;
        if (has_won(cur)) {
            game_over = true;
            winner = p1_turn() ? 2 : 1;
            return true;
        }
        if (moves == ROWS * COLS) {
            game_over = true;
            winner = 3;
        }
        return false;
    }
    bool can_play(int col) const { return !(mask & top(col)) && col >= 0 && col < COLS; }
    int cell(int row, int col) const {
        uint64_t bit = 1ULL << (col*H + (ROWS-1-row));
        if (pos1 & bit) return 1;
        if (pos2 & bit) return 2;
        return 0;
    }
    void reset() {
        pos1 = 0;
        pos2 = 0;
        mask = 0;
        moves = 0;
        game_over = false;
        winner = 0;
    }
};

static Game g_game;

// Exported functions for WASM
extern "C" {
    void init_ai() {
        init_masks();
        init_killers();
        init_history();
    }
    
    int load_book(const char* path) {
        return g_book.load(path) ? 1 : 0;
    }
    
    int load_book_from_memory(const uint8_t* data, size_t size) {
        return g_book.load_from_memory(data, size) ? 1 : 0;
    }
    
    int load_cache(const char* path) {
        return load_persistent_override(path) ? 1 : 0;
    }
    
    int load_cache_from_memory(const uint8_t* data, size_t size) {
        return load_persistent_override_from_memory(data, size) ? 1 : 0;
    }
    
    void copy_to_memory(uint8_t* dest, const uint8_t* src, size_t size) {
        memcpy(dest, src, size);
    }
    
    int save_cache(const char* path) {
        return save_persistent_override(path) ? 1 : 0;
    }
    
    void reset_game() {
        g_game.reset();
        g_tt.clear();
        init_killers();
        init_history();
    }
    
    int play_move(int col) {
        if (g_game.game_over) return -1;
        if (!g_game.can_play(col)) return -2;
        return g_game.play(col) ? 1 : 0;
    }
    
    int get_cell(int row, int col) {
        return g_game.cell(row, col);
    }
    
    int is_game_over() {
        return g_game.game_over ? 1 : 0;
    }
    
    int get_winner() {
        return g_game.winner;
    }
    
    int get_best_move(uint32_t pos1_low, uint32_t pos1_high, uint32_t pos2_low, uint32_t pos2_high, uint32_t mask_low, uint32_t mask_high, int moves) {
        // Recombine 64-bit values
        uint64_t pos1 = ((uint64_t)pos1_high << 32) | pos1_low;
        uint64_t pos2 = ((uint64_t)pos2_high << 32) | pos2_low;
        uint64_t mask = ((uint64_t)mask_high << 32) | mask_low;
        
        // Determine current player position
        uint64_t cur_pos = (moves % 2 == 0) ? pos1 : pos2;
        
        return get_best_move(cur_pos, mask, moves);
    }
}
