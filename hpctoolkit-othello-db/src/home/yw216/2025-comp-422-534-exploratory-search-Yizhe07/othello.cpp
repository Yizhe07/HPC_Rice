#include <cstdio>
#include <cstdlib>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>

//
// Macros & Constants
//

// Define a cutoff depth for switching to serial execution
#define CUTOFF_DEPTH 4

#define BIT 0x1

// Colors
#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1 - (c))

// Convert (row,col) to bit index. Rows/columns are 1..8 from user perspective.
#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1ULL << BOARD_BIT_INDEX(row,col))

// Convert a Move to the corresponding single bit in the 64-bit board
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

// Predefined row/col masks for 8
#define ROW8 ( BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) | \
               BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8) )

#define COL8 ( BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) | \
               BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8) )

// For convenience when shifting left or right
#define COL1 (COL8 << 7)

// Check if a Move is outside the board
#define IS_MOVE_OFF_BOARD(m) ( (m.row < 1) || (m.row > 8) || (m.col < 1) || (m.col > 8) )
// Offsets can be diagonals if row != 0 && col != 0
#define IS_DIAGONAL_MOVE(m) ( (m.row != 0) && (m.col != 0) )
#define MOVE_OFFSET_TO_BIT_OFFSET(m) ( (m.row * 8) + (m.col) )

//
// Type Definitions
//
typedef unsigned long long ull;

typedef struct {
    ull disks[2];  // disks[0] = X_BLACK, disks[1] = O_WHITE
} Board;

typedef struct {
    int row;
    int col;
} Move;

// Global array of direction offsets (8 directions)
Move offsets[] = {
    {0,1},   {0,-1},  // right, left
    {-1,0},  {1,0},   // up, down
    {-1,-1}, {-1,1},  // up-left, up-right
    {1,1},   {1,-1}   // down-right, down-left
};
int noffsets = sizeof(offsets)/sizeof(Move);

// Disk color characters:  0='.', 1='X', 2='O', 3='I'(unused)
char diskcolor[] = { '.', 'X', 'O', 'I' };

//
// Initial Board
// (4,5) and (5,4) => X (black), (4,4) and (5,5) => O (white)
//
Board start = {
    (BOARD_BIT(4,5) | BOARD_BIT(5,4)), // X_BLACK
    (BOARD_BIT(4,4) | BOARD_BIT(5,5))  // O_WHITE
};

//
// Forward Declarations of Key Functions
//
void PrintBoard(Board b);
int HumanTurn(Board *b, int color);
int CountBitsOnBoard(const Board *b, int color);
void EndGame(Board b);

// -----------------------------------------------------------------------------
// Board Printing
// -----------------------------------------------------------------------------
static void PrintDisk(int x_black, int o_white) {
    printf(" %c", diskcolor[x_black + (o_white << 1)]);
}

static void PrintBoardRow(int x_black, int o_white, int disks) {
    if (disks > 1) {
        PrintBoardRow(x_black >> 1, o_white >> 1, disks - 1);
    }
    PrintDisk(x_black & BIT, o_white & BIT);
}

static void PrintBoardRows(ull x_black, ull o_white, int rowsleft) {
    if (rowsleft > 1) {
        PrintBoardRows(x_black >> 8, o_white >> 8, rowsleft - 1);
    }
    printf("%d", rowsleft);
    PrintBoardRow((int)(x_black & ROW8), (int)(o_white & ROW8), 8);
    printf("\n");
}

void PrintBoard(Board b) {
    printf("  1 2 3 4 5 6 7 8\n");
    PrintBoardRows(b.disks[X_BLACK], b.disks[O_WHITE], 8);
}

// -----------------------------------------------------------------------------
// Disk Placement & Flipping
// -----------------------------------------------------------------------------
static void PlaceOrFlip(Move m, Board *b, int color) {
    ull bit = MOVE_TO_BOARD_BIT(m);
    b->disks[color] |= bit;
    b->disks[OTHERCOLOR(color)] &= ~bit;
}

// Recursive helper to flip in one direction
static int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove) {
    // Check the next square in the given direction
    Move next;
    next.row = m.row + offset.row;
    next.col = m.col + offset.col;

    if (!IS_MOVE_OFF_BOARD(next)) {
        ull nextbit = MOVE_TO_BOARD_BIT(next);
        if (nextbit & b->disks[OTHERCOLOR(color)]) {
            // The next square has an opponent disk => keep going
            int nflips = TryFlips(next, offset, b, color, verbose, domove);
            if (nflips) {
                // We can flip this one
                if (verbose) {
                    printf("flipping disk at %d,%d\n", next.row, next.col);
                }
                if (domove) PlaceOrFlip(next, b, color);
                return nflips + 1;
            }
        } else if (nextbit & b->disks[color]) {
            // Found one of my own disks => success anchor
            return 1;
        }
    }
    return 0;
}

int FlipDisks(Move m, Board *b, int color, int verbose, int domove) {
    int i;
    int nflips = 0;
    for (i = 0; i < noffsets; i++) {
        int flipresult = TryFlips(m, offsets[i], b, color, verbose, domove);
        // If flipresult > 0 => it includes +1 for the “anchor”
        if (flipresult > 0) {
            nflips += (flipresult - 1);
        }
    }
    return nflips;
}

// -----------------------------------------------------------------------------
// Human Move
// -----------------------------------------------------------------------------
void ReadMove(int color, Board *b) {
    Move m;
    ull movebit;
    for (;;) {
        printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
        scanf("%d,%d", &m.row, &m.col);

        // If move is not on the board => illegal
        if (IS_MOVE_OFF_BOARD(m)) {
            printf("Illegal move: row and column must both be between 1 and 8\n");
            PrintBoard(*b);
            continue;
        }
        movebit = MOVE_TO_BOARD_BIT(m);

        // If position is occupied => illegal
        if (movebit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
            printf("Illegal move: board position already occupied.\n");
            PrintBoard(*b);
            continue;
        }

        // Check if we flip any disks
        {
            int nflips = FlipDisks(m, b, color, 1, 1);
            if (nflips == 0) {
                printf("Illegal move: no disks flipped\n");
                PrintBoard(*b);
                continue;
            }
            PlaceOrFlip(m, b, color);
            printf("You flipped %d disks\n", nflips);
            PrintBoard(*b);
        }
        break; // success
    }
}

// Return 1 if move was made, 0 if none possible
int HumanTurn(Board *b, int color) {
    // Need to see if there is a legal move at all
    // We'll define EnumerateLegalMoves below and use it here
    // if no moves => pass
    extern int EnumerateLegalMoves(Board b, int color, Board *legal_moves);
    Board legal_moves;
    int num_moves = EnumerateLegalMoves(*b, color, &legal_moves);
    if (num_moves > 0) {
        // prompt user for row,col
        ReadMove(color, b);
        return 1;
    }
    return 0; // pass
}

// -----------------------------------------------------------------------------
// Enumerating Legal Moves
// -----------------------------------------------------------------------------
static Board NeighborMoves(Board b, int color) {
    Board neighbors = {0ULL, 0ULL};
    // For each direction offset
    for (int i = 0; i < noffsets; i++) {
        ull colmask = (offsets[i].col != 0)
                        ? ((offsets[i].col > 0) ? COL1 : COL8)
                        : 0ULL;
        int off = MOVE_OFFSET_TO_BIT_OFFSET(offsets[i]);

        if (off > 0) {
            neighbors.disks[color] |= (b.disks[OTHERCOLOR(color)] >> off) & ~colmask;
        } else {
            neighbors.disks[color] |= (b.disks[OTHERCOLOR(color)] << -off) & ~colmask;
        }
    }
    // Exclude squares already occupied
    neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
    return neighbors;
}

int EnumerateLegalMoves(Board b, int color, Board *legal_moves) {
    legal_moves->disks[color] = 0ULL;
    Board neighbors = NeighborMoves(b, color);
    ull my_neighbor_moves = neighbors.disks[color];
    int num_moves = 0;

    // Use bit-scan to find all set bits efficiently
    while (my_neighbor_moves) {
        ull next_move = my_neighbor_moves & -my_neighbor_moves; // Extract LSB
        int bitpos = __builtin_ctzll(next_move);
        int row = 8 - (bitpos / 8);
        int col = 8 - (bitpos % 8);
        Move m = { row, col };
        if (FlipDisks(m, &b, color, /*verbose=*/0, /*domove=*/0) > 0) {
            legal_moves->disks[color] |= next_move;
            num_moves++;
        }
        my_neighbor_moves ^= next_move; // Clear the processed bit
    }
    return num_moves;
}

// -----------------------------------------------------------------------------
// Counting and Ending the Game
// -----------------------------------------------------------------------------
int CountBitsOnBoard(const Board *b, int color) {
    ull bits = b->disks[color];
    int ndisks = 0;
    while (bits) {
        bits &= (bits - 1); // pop least significant set bit
        ndisks++;
    }
    return ndisks;
}

void EndGame(Board b) {
    int o_score = CountBitsOnBoard(&b, O_WHITE);
    int x_score = CountBitsOnBoard(&b, X_BLACK);
    printf("Game over.\n");
    if (o_score == x_score) {
        printf("Tie game. Each player has %d disks\n", o_score);
    } else {
        printf("X has %d disks. O has %d disks. %c wins.\n",
               x_score, o_score, (x_score > o_score ? 'X' : 'O'));
    }
}

// -----------------------------------------------------------------------------
// Utilities for Search
// -----------------------------------------------------------------------------

// Check if neither side can move => game is effectively over
bool GameIsOver(const Board &b) {
    Board dummy;
    int xMoves = EnumerateLegalMoves(b, X_BLACK, &dummy);
    int oMoves = EnumerateLegalMoves(b, O_WHITE, &dummy);
    return (xMoves == 0 && oMoves == 0);
}

// Evaluate the board from the perspective of 'color'
// (0 => X, 1 => O). The simplest approach: (#my disks - #opponent disks).
int EvaluateBoard(const Board &b, int color) {
    int myCount  = CountBitsOnBoard(&b, color);
    int oppCount = CountBitsOnBoard(&b, OTHERCOLOR(color));
    // If color==X_BLACK => score = myCount - oppCount.
    // If color==O_WHITE => score = myCount - oppCount, etc.
    // This function is from the perspective of color, so:
    return (myCount - oppCount);
}

// Copy 'oldBoard', place a disk of 'color' at move 'm' (flips included)
static int MakeMove(const Board *oldBoard, int color, Move m, Board *newBoard) {
    *newBoard = *oldBoard;  // copy
    int flipped = FlipDisks(m, newBoard, color, /*verbose=*/0, /*domove=*/1);
    PlaceOrFlip(m, newBoard, color);
    return flipped;
}

// -----------------------------------------------------------------------------
// Parallel Negamax
// -----------------------------------------------------------------------------

// Basic parallel Negamax (without alpha-beta) returns the best score for 'color'.
// 'depth' is how many moves ahead to explore.

int Negamax(const Board &b, int color, int depth) {
    // Base conditions
    if (depth == 0 || GameIsOver(b)) {
        return EvaluateBoard(b, color);
    }

    // Gather all legal moves
    Board legalMoves;
    int numMoves = EnumerateLegalMoves(b, color, &legalMoves);
    if (numMoves == 0) {
        return -Negamax(b, OTHERCOLOR(color), depth - 1);
    }

    // Use serial execution for depths below the cutoff to optimize granularity
    if (depth <= CUTOFF_DEPTH) {
        int bestValue = -9999999;
        ull bits = legalMoves.disks[color];
        for (int row = 8; row >= 1; row--) {
            ull thisrow = bits & ROW8;
            for (int col = 8; thisrow && col >= 1; col--) {
                if (thisrow & COL8) {
                    Move m = {row, col};
                    Board child;
                    MakeMove(&b, color, m, &child);
                    int val = -Negamax(child, OTHERCOLOR(color), depth - 1);
                    if (val > bestValue) bestValue = val;
                }
                thisrow >>= 1;
            }
            bits >>= 8;
        }
        return bestValue;
    } else {
        cilk::reducer< cilk::op_max<int> > bestValue(-9999999);
        Move moveList[64];
        int idx = 0;
        ull bits = legalMoves.disks[color];
        for (int row = 8; row >= 1; row--) {
            ull thisrow = bits & ROW8;
            for (int col = 8; thisrow && col >= 1; col--) {
                if (thisrow & COL8) {
                    moveList[idx].row = row;
                    moveList[idx].col = col;
                    idx++;
                }
                thisrow >>= 1;
            }
            bits >>= 8;
        }

        cilk_for (int i = 0; i < idx; i++) {
            Board child;
            MakeMove(&b, color, moveList[i], &child);
            int val = -Negamax(child, OTHERCOLOR(color), depth - 1);
            bestValue->calc_max(val);
        }

        return bestValue.get_value();
    }
}


// Usually we want to retrieve not only the best score but also the best Move.
// We'll define a "root" function that enumerates moves, spawns child calls, 
// and then picks the best index.

int NegamaxRoot(const Board &b, int color, int depth, Move *bestMove) {
    Board legalMoves;
    int numMoves = EnumerateLegalMoves(b, color, &legalMoves);
    if (numMoves == 0) {
        bestMove->row = 0;
        bestMove->col = 0;
        return -Negamax(b, OTHERCOLOR(color), depth - 1);
    }

    Move moveList[64];
    int idx = 0;
    ull moves = legalMoves.disks[color];
    while (moves) {
        ull next = moves & -moves;
        int bitpos = __builtin_ctzll(next);
        int row = 8 - (bitpos / 8);
        int col = 8 - (bitpos % 8);
        moveList[idx].row = row;
        moveList[idx].col = col;
        idx++;
        moves ^= next;
    }

    int scores[64];
    cilk_for (int i = 0; i < idx; i++) {
        Board child;
        MakeMove(&b, color, moveList[i], &child);
        scores[i] = -Negamax(child, OTHERCOLOR(color), depth - 1);
    }

    int bestVal = scores[0];
    int bestIdx = 0;
    for (int i = 1; i < idx; i++) {
        if (scores[i] > bestVal) {
            bestVal = scores[i];
            bestIdx = i;
        }
    }
    *bestMove = moveList[bestIdx];
    return bestVal;
}

// -----------------------------------------------------------------------------
// Computer Turn
// -----------------------------------------------------------------------------
int ComputerTurn(Board *b, int color, int depth) {
    // Check if there's a legal move
    Board legal;
    int num_moves = EnumerateLegalMoves(*b, color, &legal);
    if (num_moves == 0) {
        // pass
        return 0;
    }

    // Otherwise, do a parallel search
    Move bestM;
    int bestScore = NegamaxRoot(*b, color, depth, &bestM);

    // Apply that best move to 'b'
    printf("\n[%c] Computer chooses move (%d, %d) => Score: %d\n",
           (color==X_BLACK ? 'X':'O'), bestM.row, bestM.col, bestScore);

    // Flip disks on the real board
    int flips = FlipDisks(bestM, b, color, /*verbose=*/1, /*domove=*/1);
    PlaceOrFlip(bestM, b, color);

    printf("%c flipped %d disks.\n", (color==X_BLACK ? 'X':'O'), flips);
    PrintBoard(*b);
    return 1; 
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main() {
    Board gameboard = start;
    PrintBoard(gameboard);

    // Prompt whether p1 (X) is human or computer
    char p1type, p2type;
    int p1depth = 0, p2depth = 0;

    printf("Is Player 1 (X) [h]uman or [c]omputer? ");
    scanf(" %c", &p1type);
    if (p1type == 'c') {
        printf("Enter search depth for X (1..60): ");
        scanf("%d", &p1depth);
    }

    // Prompt whether p2 (O) is human or computer
    printf("Is Player 2 (O) [h]uman or [c]omputer? ");
    scanf(" %c", &p2type);
    if (p2type == 'c') {
        printf("Enter search depth for O (1..60): ");
        scanf("%d", &p2depth);
    }

    // Now alternate turns until neither side can move
    int currentColor = X_BLACK; 
    int movePossibleX = 1, movePossibleO = 1;

    while (true) {
        int moveMade = 0;
        if (currentColor == X_BLACK) {
            if (p1type == 'h') {
                moveMade = HumanTurn(&gameboard, X_BLACK);
            } else {
                moveMade = ComputerTurn(&gameboard, X_BLACK, p1depth);
            }
            movePossibleX = moveMade;
        } else {
            if (p2type == 'h') {
                moveMade = HumanTurn(&gameboard, O_WHITE);
            } else {
                moveMade = ComputerTurn(&gameboard, O_WHITE, p2depth);
            }
            movePossibleO = moveMade;
        }

        // If neither side can move => game is over
        if (!movePossibleX && !movePossibleO) {
            break;
        }

        // Switch player
        currentColor = 1 - currentColor; 
    }

    // Print final results
    EndGame(gameboard);
    return 0;
}

