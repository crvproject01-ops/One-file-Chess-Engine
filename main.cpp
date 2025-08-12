#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <map>
#include <chrono>

typedef unsigned long long U64;

// Enum for pieces for clarity
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
// Enum for colors
enum { WHITE, BLACK };

const int INF = 999999;
const int MATE = 100000;
const int MAX_QUIESCENCE_DEPTH = 6;
const int MAX_PLY = 128;

// Bitboard masks
U64 KingMoves[64], KnightMoves[64];

// Search optimization structures
struct HistoryTable {
    int scores[2][64][64]; // [side][from][to]
    
    void init() {
        memset(scores, 0, sizeof(scores));
    }
    
    void update(int side, int from, int to, int depth) {
        scores[side][from][to] += depth * depth;
        // Aging - prevent overflow
        if (scores[side][from][to] > 100000) {
            for (int s = 0; s < 2; s++)
                for (int f = 0; f < 64; f++)
                    for (int t = 0; t < 64; t++)
                        scores[s][f][t] /= 2;
        }
    }
    
    int get(int side, int from, int to) {
        return scores[side][from][to];
    }
} historyTable;

struct KillerMoves {
    struct Move* killers[MAX_PLY][2];
    
    void init() {
        memset(killers, 0, sizeof(killers));
    }
    
    void update(struct Move* m, int ply) {
        if (killers[ply][0] != m) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = m;
        }
    }
    
    bool isKiller(struct Move* m, int ply) {
        return killers[ply][0] == m || killers[ply][1] == m;
    }
} killerMoves;

// Transposition Table
struct TTEntry {
    U64 hash;
    int depth;
    int score;
    int flag; // EXACT, ALPHA, BETA
    int bestMove;
};

const int TT_SIZE = 1 << 20; // 1MB entries
TTEntry transpositionTable[TT_SIZE];

enum { TT_EXACT, TT_ALPHA, TT_BETA };

// UCI Options
struct UCIOptions {
    int depth;
    bool useQuiescence;
    int quiescenceDepth;
    
    UCIOptions() : depth(8), useQuiescence(true), quiescenceDepth(4) {}
} uciOptions;

// Statistics
struct SearchStats {
    long long nodes;
    long long qnodes;
    int currentDepth;
    std::chrono::steady_clock::time_point startTime;
    
    void init() {
        nodes = 0;
        qnodes = 0;
        currentDepth = 0;
        startTime = std::chrono::steady_clock::now();
    }
    
    long long nps() {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (ms == 0) return 0;
        return (nodes + qnodes) * 1000 / ms;
    }
} searchStats;

// Forward declarations
struct Board;
struct Move;
bool is_attacked(int sq, int side, const Board& b);
void makeMove(Board& b, const Move& m);
bool isInCheck(const Board& b);
bool isLegalMove(Board& b, const Move& m);
U64 get_rook_attacks(int sq, U64 blockers);
U64 get_bishop_attacks(int sq, U64 blockers);
U64 zobristHash(const Board& b);

// Board representation
struct Board {
    U64 pieces[2][6];
    U64 occupied[2];
    U64 all;
    int side, ep, castle;
    U64 hash;

    void init() {
        memset(pieces, 0, sizeof(pieces));
        pieces[WHITE][PAWN]   = 0xFF00ULL;
        pieces[WHITE][KNIGHT] = 0x42ULL;
        pieces[WHITE][BISHOP] = 0x24ULL;
        pieces[WHITE][ROOK]   = 0x81ULL;
        pieces[WHITE][QUEEN]  = 0x8ULL;
        pieces[WHITE][KING]   = 0x10ULL;

        pieces[BLACK][PAWN]   = 0xFF000000000000ULL;
        pieces[BLACK][KNIGHT] = 0x4200000000000000ULL;
        pieces[BLACK][BISHOP] = 0x2400000000000000ULL;
        pieces[BLACK][ROOK]   = 0x8100000000000000ULL;
        pieces[BLACK][QUEEN]  = 0x800000000000000ULL;
        pieces[BLACK][KING]   = 0x1000000000000000ULL;

        update();
        side = WHITE;
        ep = -1;
        castle = 15;
        hash = zobristHash(*this);
    }

    void update() {
        occupied[WHITE] = occupied[BLACK] = 0;
        for (int p = PAWN; p <= KING; p++) {
            occupied[WHITE] |= pieces[WHITE][p];
            occupied[BLACK] |= pieces[BLACK][p];
        }
        all = occupied[WHITE] | occupied[BLACK];
    }

    int evaluate() {
        int eval = 0;
        int values[] = {100, 320, 330, 500, 900, 0};
        
        // Fast material count
        for (int c = 0; c < 2; c++) {
            for (int p = 0; p < 6; p++) {
                int count = __builtin_popcountll(pieces[c][p]);
                eval += (c == WHITE ? count : -count) * values[p];
            }
        }
        
        // King safety (simplified for speed)
        for (int c = 0; c < 2; c++) {
            if (!pieces[c][KING]) continue;
            int king_sq = __builtin_ctzll(pieces[c][KING]);
            
            // Castling bonus
            if (c == WHITE) {
                if (king_sq == 6 || king_sq == 2) eval += 40;
                else if (king_sq == 4) eval -= 20;
            } else {
                if (king_sq == 62 || king_sq == 58) eval -= 40;
                else if (king_sq == 60) eval += 20;
            }
        }
        
        // Center control (fast)
        U64 center = 0x0000001818000000ULL;
        eval += (__builtin_popcountll(pieces[WHITE][PAWN] & center) -
                 __builtin_popcountll(pieces[BLACK][PAWN] & center)) * 20;
        
        // Passed pawns (simplified)
        U64 wpawns = pieces[WHITE][PAWN];
        while (wpawns) {
            int sq = __builtin_ctzll(wpawns);
            int rank = sq / 8;
            if (rank >= 4) eval += (rank - 3) * 15;
            wpawns &= wpawns - 1;
        }
        
        U64 bpawns = pieces[BLACK][PAWN];
        while (bpawns) {
            int sq = __builtin_ctzll(bpawns);
            int rank = sq / 8;
            if (rank <= 3) eval -= (4 - rank) * 15;
            bpawns &= bpawns - 1;
        }
        
        return side == WHITE ? eval : -eval;
    }
};

struct Move {
    int from, to, score;
    int piece, captured, promo;

    Move(int f = 0, int t = 0, int p = 0, int c = -1, int pr = 0)
        : from(f), to(t), piece(p), captured(c), promo(pr), score(0) {}
        
    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promo == other.promo;
    }
};

// Zobrist hashing for transposition table
U64 zobristPieces[2][6][64];
U64 zobristCastle[16];
U64 zobristEp[64];
U64 zobristSide;

void initZobrist() {
    srand(12345);
    for (int c = 0; c < 2; c++)
        for (int p = 0; p < 6; p++)
            for (int sq = 0; sq < 64; sq++)
                zobristPieces[c][p][sq] = ((U64)rand() << 48) | ((U64)rand() << 32) | 
                                          ((U64)rand() << 16) | (U64)rand();
    
    for (int i = 0; i < 16; i++)
        zobristCastle[i] = ((U64)rand() << 48) | ((U64)rand() << 32) | 
                          ((U64)rand() << 16) | (U64)rand();
    
    for (int i = 0; i < 64; i++)
        zobristEp[i] = ((U64)rand() << 48) | ((U64)rand() << 32) | 
                      ((U64)rand() << 16) | (U64)rand();
    
    zobristSide = ((U64)rand() << 48) | ((U64)rand() << 32) | 
                  ((U64)rand() << 16) | (U64)rand();
}

U64 zobristHash(const Board& b) {
    U64 hash = 0;
    
    for (int c = 0; c < 2; c++) {
        for (int p = 0; p < 6; p++) {
            U64 bb = b.pieces[c][p];
            while (bb) {
                int sq = __builtin_ctzll(bb);
                hash ^= zobristPieces[c][p][sq];
                bb &= bb - 1;
            }
        }
    }
    
    hash ^= zobristCastle[b.castle];
    if (b.ep != -1) hash ^= zobristEp[b.ep];
    if (b.side == BLACK) hash ^= zobristSide;
    
    return hash;
}

// Attack generation
U64 get_rook_attacks(int sq, U64 blockers) {
    U64 attacks = 0;
    int r, f;
    int tr = sq / 8;
    int tf = sq % 8;

    for (r = tr + 1; r <= 7; r++) { 
        attacks |= (1ULL << (r * 8 + tf)); 
        if ((1ULL << (r * 8 + tf)) & blockers) break; 
    }
    for (r = tr - 1; r >= 0; r--) { 
        attacks |= (1ULL << (r * 8 + tf)); 
        if ((1ULL << (r * 8 + tf)) & blockers) break; 
    }
    for (f = tf + 1; f <= 7; f++) { 
        attacks |= (1ULL << (tr * 8 + f)); 
        if ((1ULL << (tr * 8 + f)) & blockers) break; 
    }
    for (f = tf - 1; f >= 0; f--) { 
        attacks |= (1ULL << (tr * 8 + f)); 
        if ((1ULL << (tr * 8 + f)) & blockers) break; 
    }
    return attacks;
}

U64 get_bishop_attacks(int sq, U64 blockers) {
    U64 attacks = 0;
    int r, f;
    int tr = sq / 8;
    int tf = sq % 8;
    
    for (r = tr + 1, f = tf + 1; r <= 7 && f <= 7; r++, f++) { 
        attacks |= (1ULL << (r * 8 + f)); 
        if ((1ULL << (r * 8 + f)) & blockers) break; 
    }
    for (r = tr + 1, f = tf - 1; r <= 7 && f >= 0; r++, f--) { 
        attacks |= (1ULL << (r * 8 + f)); 
        if ((1ULL << (r * 8 + f)) & blockers) break; 
    }
    for (r = tr - 1, f = tf + 1; r >= 0 && f <= 7; r--, f++) { 
        attacks |= (1ULL << (r * 8 + f)); 
        if ((1ULL << (r * 8 + f)) & blockers) break; 
    }
    for (r = tr - 1, f = tf - 1; r >= 0 && f >= 0; r--, f--) { 
        attacks |= (1ULL << (r * 8 + f)); 
        if ((1ULL << (r * 8 + f)) & blockers) break; 
    }
    return attacks;
}

bool is_attacked(int sq, int attacker, const Board& b) {
    if (attacker == WHITE) {
        if ((sq >= 9) && (sq % 8 != 0) && ((1ULL << (sq - 9)) & b.pieces[WHITE][PAWN])) return true;
        if ((sq >= 7) && (sq % 8 != 7) && ((1ULL << (sq - 7)) & b.pieces[WHITE][PAWN])) return true;
    } else {
        if ((sq <= 56) && (sq % 8 != 0) && ((1ULL << (sq + 7)) & b.pieces[BLACK][PAWN])) return true;
        if ((sq <= 54) && (sq % 8 != 7) && ((1ULL << (sq + 9)) & b.pieces[BLACK][PAWN])) return true;
    }

    if (KnightMoves[sq] & b.pieces[attacker][KNIGHT]) return true;
    if (KingMoves[sq] & b.pieces[attacker][KING]) return true;
    if (get_rook_attacks(sq, b.all) & (b.pieces[attacker][ROOK] | b.pieces[attacker][QUEEN])) return true;
    if (get_bishop_attacks(sq, b.all) & (b.pieces[attacker][BISHOP] | b.pieces[attacker][QUEEN])) return true;

    return false;
}

bool isInCheck(const Board& b) {
    if (b.pieces[b.side][KING] == 0) return false;
    int king_sq = __builtin_ctzll(b.pieces[b.side][KING]);
    return is_attacked(king_sq, 1 - b.side, b);
}

void makeMove(Board& b, const Move& m) {
    U64 from_bb = 1ULL << m.from;
    U64 to_bb = 1ULL << m.to;

    int opponent = 1 - b.side;
    int prev_ep = b.ep;

    // Update hash
    b.hash ^= zobristPieces[b.side][m.piece][m.from];
    b.hash ^= zobristPieces[b.side][m.piece][m.to];
    
    if (b.ep != -1) b.hash ^= zobristEp[b.ep];
    b.hash ^= zobristCastle[b.castle];

    // Update castling rights
    if (m.piece == KING) {
        if (b.side == WHITE) b.castle &= ~3;
        else b.castle &= ~12;
    }
    if (m.from == 0 || m.to == 0) b.castle &= ~2;
    if (m.from == 7 || m.to == 7) b.castle &= ~1;
    if (m.from == 56 || m.to == 56) b.castle &= ~8;
    if (m.from == 63 || m.to == 63) b.castle &= ~4;
    
    b.hash ^= zobristCastle[b.castle];
    b.ep = -1;

    // Move piece
    b.pieces[b.side][m.piece] ^= (from_bb | to_bb);

    // Handle captures
    for (int p = PAWN; p <= KING; ++p) {
        if (b.pieces[opponent][p] & to_bb) {
            b.pieces[opponent][p] ^= to_bb;
            b.hash ^= zobristPieces[opponent][p][m.to];
            break;
        }
    }

    // Handle special moves
    if (m.piece == PAWN) {
        if (m.to == prev_ep) {
            int captured_pawn_sq = (b.side == WHITE) ? m.to - 8 : m.to + 8;
            b.pieces[opponent][PAWN] ^= (1ULL << captured_pawn_sq);
            b.hash ^= zobristPieces[opponent][PAWN][captured_pawn_sq];
        }
        if (std::abs(m.from - m.to) == 16) {
            b.ep = (b.side == WHITE) ? m.from + 8 : m.from - 8;
            b.hash ^= zobristEp[b.ep];
        }
        if (m.promo) {
            b.pieces[b.side][PAWN] ^= to_bb;
            b.pieces[b.side][m.promo] ^= to_bb;
            b.hash ^= zobristPieces[b.side][PAWN][m.to];
            b.hash ^= zobristPieces[b.side][m.promo][m.to];
        }
    } else if (m.piece == KING) {
        if (std::abs(m.from - m.to) == 2) {
            if (m.to == 6) {
                b.pieces[WHITE][ROOK] ^= (1ULL << 7 | 1ULL << 5);
                b.hash ^= zobristPieces[WHITE][ROOK][7];
                b.hash ^= zobristPieces[WHITE][ROOK][5];
            } else if (m.to == 2) {
                b.pieces[WHITE][ROOK] ^= (1ULL << 0 | 1ULL << 3);
                b.hash ^= zobristPieces[WHITE][ROOK][0];
                b.hash ^= zobristPieces[WHITE][ROOK][3];
            } else if (m.to == 62) {
                b.pieces[BLACK][ROOK] ^= (1ULL << 63 | 1ULL << 61);
                b.hash ^= zobristPieces[BLACK][ROOK][63];
                b.hash ^= zobristPieces[BLACK][ROOK][61];
            } else if (m.to == 58) {
                b.pieces[BLACK][ROOK] ^= (1ULL << 56 | 1ULL << 59);
                b.hash ^= zobristPieces[BLACK][ROOK][56];
                b.hash ^= zobristPieces[BLACK][ROOK][59];
            }
        }
    }

    b.update();
    b.side = opponent;
    b.hash ^= zobristSide;
}

bool isLegalMove(Board& b, const Move& m) {
    Board copy = b;
    makeMove(copy, m);
    if (copy.pieces[b.side][KING] == 0) return false;
    int king_sq = __builtin_ctzll(copy.pieces[b.side][KING]);
    return !is_attacked(king_sq, copy.side, copy);
}

// Move generation optimized for ordering
std::vector<Move> generateMoves(Board& b, bool capturesOnly = false) {
    std::vector<Move> moves;
    moves.reserve(capturesOnly ? 32 : 128);
    U64 bitboard, attacks;
    int from, to;

    for (int p = PAWN; p <= KING; p++) {
        bitboard = b.pieces[b.side][p];
        while (bitboard) {
            from = __builtin_ctzll(bitboard);
            attacks = 0;

            if (p == PAWN) {
                int dir = (b.side == WHITE) ? 8 : -8;
                int promo_rank = (b.side == WHITE) ? 7 : 0;
                
                if (!capturesOnly) {
                    // Single push
                    int to_sq = from + dir;
                    if (to_sq >= 0 && to_sq < 64 && !(b.all & (1ULL << to_sq))) {
                        if ((to_sq / 8) == promo_rank) {
                            moves.push_back(Move(from, to_sq, p, -1, QUEEN));
                        } else {
                            moves.push_back(Move(from, to_sq, p));
                            // Double push
                            int start_rank = (b.side == WHITE) ? 1 : 6;
                            if ((from / 8) == start_rank) {
                                int to_sq2 = from + 2 * dir;
                                if (!(b.all & (1ULL << to_sq2))) {
                                    moves.push_back(Move(from, to_sq2, p));
                                }
                            }
                        }
                    }
                }
                
                // Captures
                int cap_dirs[] = {dir - 1, dir + 1};
                for (int d : cap_dirs) {
                    to = from + d;
                    if (to < 0 || to > 63 || std::abs((from % 8) - (to % 8)) > 1) continue;

                    if (b.occupied[1 - b.side] & (1ULL << to)) {
                        if ((to / 8) == promo_rank) {
                            moves.push_back(Move(from, to, p, 0, QUEEN));
                        } else {
                            moves.push_back(Move(from, to, p));
                        }
                    } else if (!capturesOnly && to == b.ep) {
                        moves.push_back(Move(from, to, p));
                    }
                }
            } else if (p == KING && !capturesOnly) {
                attacks = KingMoves[from] & ~b.occupied[b.side];
                // Castling
                if (!isInCheck(b)) {
                    if (b.side == WHITE) {
                        if ((b.castle & 1) && !(b.all & 0x60ULL)) {
                            if (!is_attacked(5, BLACK, b) && !is_attacked(6, BLACK, b)) {
                                moves.push_back(Move(4, 6, KING));
                            }
                        }
                        if ((b.castle & 2) && !(b.all & 0xEULL)) {
                            if (!is_attacked(3, BLACK, b) && !is_attacked(2, BLACK, b)) {
                                moves.push_back(Move(4, 2, KING));
                            }
                        }
                    } else {
                        if ((b.castle & 4) && !(b.all & 0x6000000000000000ULL)) {
                            if (!is_attacked(61, WHITE, b) && !is_attacked(62, WHITE, b)) {
                                moves.push_back(Move(60, 62, KING));
                            }
                        }
                        if ((b.castle & 8) && !(b.all & 0xE00000000000000ULL)) {
                            if (!is_attacked(59, WHITE, b) && !is_attacked(58, WHITE, b)) {
                                moves.push_back(Move(60, 58, KING));
                            }
                        }
                    }
                }
            } else {
                if (p == KNIGHT) attacks = KnightMoves[from];
                else if (p == BISHOP) attacks = get_bishop_attacks(from, b.all);
                else if (p == ROOK) attacks = get_rook_attacks(from, b.all);
                else if (p == QUEEN) attacks = (get_rook_attacks(from, b.all) | get_bishop_attacks(from, b.all));
                else if (p == KING) attacks = KingMoves[from];
                
                if (capturesOnly) {
                    attacks &= b.occupied[1 - b.side];
                } else {
                    attacks &= ~b.occupied[b.side];
                }
            }

            while (attacks) {
                to = __builtin_ctzll(attacks);
                moves.push_back(Move(from, to, p));
                attacks &= attacks - 1;
            }

            bitboard &= bitboard - 1;
        }
    }

    // Filter illegal moves
    std::vector<Move> legalMoves;
    legalMoves.reserve(moves.size());
    
    for (auto& m : moves) {
        if (isLegalMove(b, m)) {
            legalMoves.push_back(m);
        }
    }

    return legalMoves;
}

// Move ordering for better pruning
void scoreMoves(std::vector<Move>& moves, Board& b, Move* ttMove, int ply) {
    for (auto& m : moves) {
        // TT move gets highest priority
        if (ttMove && m == *ttMove) {
            m.score = 1000000;
            continue;
        }
        
        // MVV-LVA for captures
        if (b.occupied[1 - b.side] & (1ULL << m.to)) {
            int victimValues[] = {100, 300, 300, 500, 900, 10000};
            for (int p = KING; p >= PAWN; p--) {
                if (b.pieces[1 - b.side][p] & (1ULL << m.to)) {
                    m.score = 100000 + victimValues[p] * 10 - victimValues[m.piece];
                    break;
                }
            }
        }
        // Killer moves
        else if (killerMoves.isKiller(&m, ply)) {
            m.score = 90000;
        }
        // History heuristic
        else {
            m.score = historyTable.get(b.side, m.from, m.to);
        }
        
        // Promotion bonus
        if (m.promo == QUEEN) m.score += 80000;
    }
    
    std::sort(moves.begin(), moves.end(), 
              [](const Move& a, const Move& b) { return a.score > b.score; });
}

// Quiescence search
int quiescence(Board& b, int alpha, int beta, int depth) {
    searchStats.qnodes++;
    
    int stand_pat = b.evaluate();
    
    if (stand_pat >= beta) return beta;
    if (alpha < stand_pat) alpha = stand_pat;
    if (depth <= -MAX_QUIESCENCE_DEPTH) return stand_pat;
    
    auto captures = generateMoves(b, true);
    scoreMoves(captures, b, nullptr, 0);
    
    for (const auto& m : captures) {
        // Delta pruning
        int gain = 200; // Expected gain from capture
        if (m.piece != PAWN) gain = 900;
        if (stand_pat + gain < alpha && depth < -1) continue;
        
        Board copy = b;
        makeMove(copy, m);
        
        int score = -quiescence(copy, -beta, -alpha, depth - 1);
        
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    
    return alpha;
}

// Main alpha-beta search with advanced pruning
int search(Board& b, int depth, int alpha, int beta, Move& bestMove, int ply, bool nullMove = true) {
    searchStats.nodes++;
    
    // Check extension
    bool inCheck = isInCheck(b);
    if (inCheck) depth++;
    
    // Transposition table lookup
    int ttIndex = b.hash % TT_SIZE;
    TTEntry* ttEntry = &transpositionTable[ttIndex];
    Move ttMove;
    
    if (ttEntry->hash == b.hash && ttEntry->depth >= depth) {
        if (ttEntry->flag == TT_EXACT) {
            if (ply == 0) {
                bestMove.from = ttEntry->bestMove & 63;
                bestMove.to = (ttEntry->bestMove >> 6) & 63;
                bestMove.piece = (ttEntry->bestMove >> 12) & 7;
            }
            return ttEntry->score;
        }
        if (ttEntry->flag == TT_ALPHA && ttEntry->score <= alpha) return alpha;
        if (ttEntry->flag == TT_BETA && ttEntry->score >= beta) return beta;
    }
    
    if (ttEntry->hash == b.hash && ttEntry->bestMove) {
        ttMove.from = ttEntry->bestMove & 63;
        ttMove.to = (ttEntry->bestMove >> 6) & 63;
        ttMove.piece = (ttEntry->bestMove >> 12) & 7;
    }
    
    if (depth <= 0) {
        return quiescence(b, alpha, beta, 0);
    }
    
    // Null move pruning
    if (nullMove && !inCheck && depth >= 3 && ply > 0) {
        Board copy = b;
        copy.side = 1 - copy.side;
        copy.hash ^= zobristSide;
        copy.ep = -1;
        
        Move dummy;
        int R = depth > 6 ? 3 : 2; // Reduction factor
        int score = -search(copy, depth - 1 - R, -beta, -beta + 1, dummy, ply + 1, false);
        
        if (score >= beta) {
            return beta; // Null move cutoff
        }
    }
    
    auto moves = generateMoves(b);
    
    if (moves.empty()) {
        if (inCheck) return -MATE + ply;
        return 0;
    }
    
    scoreMoves(moves, b, ttEntry->hash == b.hash ? &ttMove : nullptr, ply);
    
    if (ply == 0 && !moves.empty()) {
        bestMove = moves[0];
    }
    
    int moveCount = 0;
    int bestScore = -INF;
    Move localBest;
    int origAlpha = alpha;
    
    for (auto& m : moves) {
        moveCount++;
        
        // Late Move Reduction (LMR)
        int reduction = 0;
        if (moveCount > 4 && depth >= 3 && !inCheck && 
            !(b.occupied[1 - b.side] & (1ULL << m.to)) && m.promo == 0) {
            
            if (moveCount > 12) reduction = 3;
            else if (moveCount > 6) reduction = 2;
            else reduction = 1;
            
            // Reduce less for killers and high history scores
            if (killerMoves.isKiller(&m, ply) || historyTable.get(b.side, m.from, m.to) > 5000) {
                reduction = std::max(0, reduction - 1);
            }
        }
        
        Board copy = b;
        makeMove(copy, m);
        
        int score;
        
        // Principal Variation Search (PVS)
        if (moveCount == 1) {
            // Search first move with full window
            Move dummy;
            score = -search(copy, depth - 1 - reduction, -beta, -alpha, dummy, ply + 1, true);
        } else {
            // Search with null window
            Move dummy;
            score = -search(copy, depth - 1 - reduction, -alpha - 1, -alpha, dummy, ply + 1, true);
            
            // Re-search if failed high
            if (score > alpha && score < beta) {
                score = -search(copy, depth - 1, -beta, -alpha, dummy, ply + 1, true);
            }
        }
        
        // Re-search without reduction if reduced search failed high
        if (reduction > 0 && score > alpha) {
            Move dummy;
            score = -search(copy, depth - 1, -beta, -alpha, dummy, ply + 1, true);
        }
        
        if (score > bestScore) {
            bestScore = score;
            localBest = m;
            
            if (ply == 0) {
                bestMove = m;
            }
        }
        
        if (score > alpha) {
            alpha = score;
            
            // Update history
            if (!(b.occupied[1 - b.side] & (1ULL << m.to))) {
                historyTable.update(b.side, m.from, m.to, depth);
            }
        }
        
        if (alpha >= beta) {
            // Beta cutoff - update killers
            if (!(b.occupied[1 - b.side] & (1ULL << m.to))) {
                killerMoves.update(&m, ply);
            }
            break;
        }
        
        // Futility pruning
        if (depth <= 2 && !inCheck && moveCount > 8 && 
            !(b.occupied[1 - b.side] & (1ULL << m.to))) {
            int futilityMargin = depth * 100;
            if (b.evaluate() + futilityMargin < alpha) {
                break; // Skip remaining moves
            }
        }
    }
    
    // Store in transposition table
    ttEntry->hash = b.hash;
    ttEntry->depth = depth;
    ttEntry->score = bestScore;
    ttEntry->bestMove = localBest.from | (localBest.to << 6) | (localBest.piece << 12);
    
    if (bestScore <= origAlpha) {
        ttEntry->flag = TT_ALPHA;
    } else if (bestScore >= beta) {
        ttEntry->flag = TT_BETA;
    } else {
        ttEntry->flag = TT_EXACT;
    }
    
    return bestScore;
}

// Iterative deepening with aspiration windows
int iterativeDeepening(Board& b, int maxDepth, Move& bestMove, int timeLimit = 0) {
    int score = 0;
    int alpha = -INF;
    int beta = INF;
    int window = 50;
    
    searchStats.init();
    
    for (int depth = 1; depth <= maxDepth; depth++) {
        searchStats.currentDepth = depth;
        
        // Aspiration window search
        if (depth >= 4) {
            alpha = score - window;
            beta = score + window;
        }
        
        int tempScore = search(b, depth, alpha, beta, bestMove, 0);
        
        // Re-search if outside window
        if (tempScore <= alpha || tempScore >= beta) {
            tempScore = search(b, depth, -INF, INF, bestMove, 0);
            window = 50; // Reset window
        } else {
            window = 25; // Narrow window for next iteration
        }
        
        score = tempScore;
        
        // Time management
        if (timeLimit > 0) {
            auto elapsed = std::chrono::steady_clock::now() - searchStats.startTime;
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            
            // Stop if we've used 40% of our time and depth > 4
            if (ms > timeLimit * 0.4 && depth > 4) {
                break;
            }
        }
        
        // Output UCI info
        std::cout << "info depth " << depth;
        std::cout << " score ";
        
        if (std::abs(score) >= MATE - 1000) {
            int mateIn = (MATE - std::abs(score) + 1) / 2;
            if (score < 0) mateIn = -mateIn;
            std::cout << "mate " << mateIn;
        } else {
            std::cout << "cp " << score;
        }
        
        std::cout << " nodes " << searchStats.nodes;
        std::cout << " nps " << searchStats.nps();
        std::cout << " pv ";
        
        // Output best move
        std::string move_str;
        move_str += (bestMove.from % 8) + 'a';
        move_str += (bestMove.from / 8) + '1';
        move_str += (bestMove.to % 8) + 'a';
        move_str += (bestMove.to / 8) + '1';
        if (bestMove.promo) {
            switch (bestMove.promo) {
                case QUEEN: move_str += 'q'; break;
                case ROOK: move_str += 'r'; break;
                case BISHOP: move_str += 'b'; break;
                case KNIGHT: move_str += 'n'; break;
            }
        }
        std::cout << move_str << "\n";
        
        // Stop on mate found
        if (std::abs(score) >= MATE - 1000) {
            break;
        }
    }
    
    return score;
}

// Initialize lookup tables
void initTables() {
    for (int sq = 0; sq < 64; sq++) {
        int x = sq % 8, y = sq / 8;
        
        KingMoves[sq] = 0;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                    KingMoves[sq] |= 1ULL << (ny * 8 + nx);
                }
            }
        }
        
        KnightMoves[sq] = 0;
        int kdx[] = {2, 2, -2, -2, 1, 1, -1, -1};
        int kdy[] = {1, -1, 1, -1, 2, -2, 2, -2};
        for (int i = 0; i < 8; i++) {
            int nx = x + kdx[i], ny = y + kdy[i];
            if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8) {
                KnightMoves[sq] |= 1ULL << (ny * 8 + nx);
            }
        }
    }
    
    initZobrist();
    historyTable.init();
    killerMoves.init();
    memset(transpositionTable, 0, sizeof(transpositionTable));
}

// Move parser for UCI
bool parseMove(Board& b, std::string move_str, Move& parsed_move) {
    auto moves = generateMoves(b);
    int from = (move_str[0] - 'a') + (move_str[1] - '1') * 8;
    int to = (move_str[2] - 'a') + (move_str[3] - '1') * 8;
    int promo_piece = 0;

    if (move_str.length() == 5) {
        switch (move_str[4]) {
            case 'q': promo_piece = QUEEN; break;
            case 'r': promo_piece = ROOK; break;
            case 'b': promo_piece = BISHOP; break;
            case 'n': promo_piece = KNIGHT; break;
        }
    }

    for (const auto& m : moves) {
        if (m.from == from && m.to == to) {
            if (m.promo) {
                if (m.promo == promo_piece) {
                    parsed_move = m;
                    return true;
                }
            } else {
                parsed_move = m;
                return true;
            }
        }
    }
    return false;
}

int main() {
    initTables();
    Board board;
    board.init();
    
    std::string line, cmd;
    
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        iss >> cmd;
        
        if (cmd == "uci") {
            std::cout << "id name NanoChessTurbo\n";
            std::cout << "id author CrvProject\n";
            std::cout << "option name Depth type spin default 10 min 1 max 30\n";
            std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
            std::cout << "uciok\n";
        }
        else if (cmd == "setoption") {
            std::string token;
            iss >> token; // "name"
            if (token == "name") {
                std::string optionName;
                iss >> optionName;
                
                std::string nextToken;
                while (iss >> nextToken && nextToken != "value") {
                    optionName += nextToken;
                }
                
                if (optionName == "Depth") {
                    int value;
                    iss >> value;
                    uciOptions.depth = std::max(1, std::min(30, value));
                }
                else if (optionName == "Hash") {
                    int value;
                    iss >> value;
                    // Could resize TT here if needed
                }
            }
        }
        else if (cmd == "isready") {
            std::cout << "readyok\n";
        }
        else if (cmd == "ucinewgame") {
            board.init();
            historyTable.init();
            killerMoves.init();
            memset(transpositionTable, 0, sizeof(transpositionTable));
        }
        else if (cmd == "position") {
            std::string token, sub_cmd;
            iss >> sub_cmd;
            
            if (sub_cmd == "startpos") {
                board.init();
                iss >> token;
            } else if (sub_cmd == "fen") {
                board.init();
                while (iss >> token && token != "moves") {}
            }

            if (token == "moves") {
                Move m;
                while (iss >> token) {
                    if (parseMove(board, token, m)) {
                        makeMove(board, m);
                    }
                }
            }
        }
        else if (cmd == "go") {
            int searchDepth = uciOptions.depth;
            int moveTime = 0;
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            int movestogo = 40;
            bool infinite = false;
            
            std::string token;
            while (iss >> token) {
                if (token == "depth") {
                    iss >> searchDepth;
                    searchDepth = std::max(1, std::min(30, searchDepth));
                }
                else if (token == "movetime") {
                    iss >> moveTime;
                }
                else if (token == "wtime") {
                    iss >> wtime;
                }
                else if (token == "btime") {
                    iss >> btime;
                }
                else if (token == "winc") {
                    iss >> winc;
                }
                else if (token == "binc") {
                    iss >> binc;
                }
                else if (token == "movestogo") {
                    iss >> movestogo;
                }
                else if (token == "infinite") {
                    infinite = true;
                    searchDepth = 20;
                }
            }
            
            // Time allocation
            int allocatedTime = 0;
            if (!infinite && moveTime == 0 && (wtime > 0 || btime > 0)) {
                int timeLeft = (board.side == WHITE) ? wtime : btime;
                int increment = (board.side == WHITE) ? winc : binc;
                
                allocatedTime = timeLeft / movestogo + increment * 0.8;
                allocatedTime = std::min(allocatedTime, timeLeft / 3);
            } else if (moveTime > 0) {
                allocatedTime = moveTime * 0.95;
            }
            
            Move bestMove;
            iterativeDeepening(board, searchDepth, bestMove, allocatedTime);
            
            // Output best move
            if (bestMove.from != bestMove.to || bestMove.from != 0) {
                std::string move_str;
                move_str += (bestMove.from % 8) + 'a';
                move_str += (bestMove.from / 8) + '1';
                move_str += (bestMove.to % 8) + 'a';
                move_str += (bestMove.to / 8) + '1';
                if (bestMove.promo) {
                    switch (bestMove.promo) {
                        case QUEEN: move_str += 'q'; break;
                        case ROOK: move_str += 'r'; break;
                        case BISHOP: move_str += 'b'; break;
                        case KNIGHT: move_str += 'n'; break;
                    }
                }
                
                std::cout << "bestmove " << move_str << "\n";
            } else {
                auto moves = generateMoves(board);
                if (!moves.empty()) {
                    Move fallback = moves[0];
                    std::string move_str;
                    move_str += (fallback.from % 8) + 'a';
                    move_str += (fallback.from / 8) + '1';
                    move_str += (fallback.to % 8) + 'a';
                    move_str += (fallback.to / 8) + '1';
                    if (fallback.promo) {
                        switch (fallback.promo) {
                            case QUEEN: move_str += 'q'; break;
                            case ROOK: move_str += 'r'; break;
                            case BISHOP: move_str += 'b'; break;
                            case KNIGHT: move_str += 'n'; break;
                        }
                    }
                    std::cout << "bestmove " << move_str << "\n";
                } else {
                    std::cout << "bestmove 0000\n";
                }
            }
        }
        else if (cmd == "quit") {
            break;
        }
    }
    
    return 0;
}