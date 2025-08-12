# NanoChessTurbo One-file-Chess-Engine
A complete UCI chess program written in a single C++ code and with only 1000 lines. The program includes all the rules of chess, evaluation functions, and all the optimizations to speed up the search for moves.

##**Features**  

##**Search Algorithms**

-Alpha-Beta Pruning with fail-hard framework  

-Principal Variation Search (PVS) for efficient move ordering  

-Iterative Deepening with aspiration windows  

-Quiescence Search to avoid horizon effect  

-Null Move Pruning for forward pruning  

-Late Move Reduction (LMR) to reduce search depth for likely poor moves  

-Futility Pruning in shallow nodes  

-Check Extensions to avoid missing tactics

##**Evaluation**  


-Material evaluation with piece values  

-King safety evaluation with castling bonus  

-Pawn structure analysis (passed pawns, doubled pawns)  

-Center control bonus  

-Piece mobility and development  


##**Optimizations**  


-Zobrist Hashing for position identification  

-Transposition Table (1MB default) for position caching  

-History Heuristic for move ordering  

-Killer Moves for cut-off improvements  

-MVV-LVA (Most Valuable Victim - Least Valuable Attacker) for capture ordering  


##**Performance**  


-Depth 15-20 within seconds  

-Depth 10-12 almost instantly  

-max elo estimated ~2300/2400  





##**Requirements** 


-C++ compiler with C++11 support (GCC, Clang, MSVC)  

-CMake 3.10+ (optional, for CMake build)  

-Any UCI-compatible chess GUI (Arena, Lucas Chess, Cute Chess, etc.)  

##**UCI Commands**  

The engine supports the standard UCI protocol:  


uci - Initialize UCI mode  

isready - Check if engine is ready  

ucinewgame - Start a new game  

position [startpos | fen] [moves ...] - Set position  

go [depth n] [movetime n] [wtime n] [btime n] [infinite] - Start calculating  

quit - Exit the engine  


##**UCI Options**  

Configure the engine through UCI options:  


-Depth (1-30, default: 10) - Maximum search depth  

-Hash (1-1024 MB, default: 64) - Transposition table size  


Example:  

setoption name Depth value 15  

setoption name Hash value 128  

