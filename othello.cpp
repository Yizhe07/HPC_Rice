#include <cstdio>
#include <cstdlib>
#include <cilk/cilk.h>
#include <cilk/reducer_max.h>

// Define a cutoff depth for switching to serial execution
#define CUTOFF_DEPTH 4

#define BIT 0x1


#define X_BLACK 0
#define O_WHITE 1
#define OTHERCOLOR(c) (1 - (c))

/* 
	represent game board squares as a 64-bit unsigned integer.
	these macros index from a row,column position on the board
	to a position and bit in a game board bitvector
*/


#define BOARD_BIT_INDEX(row,col) ((8 - (row)) * 8 + (8 - (col)))
#define BOARD_BIT(row,col) (0x1ULL << BOARD_BIT_INDEX(row,col))
#define MOVE_TO_BOARD_BIT(m) BOARD_BIT(m.row, m.col)

/* all of the bits in the row 8 */
#define ROW8 ( BOARD_BIT(8,1) | BOARD_BIT(8,2) | BOARD_BIT(8,3) | BOARD_BIT(8,4) | \
               BOARD_BIT(8,5) | BOARD_BIT(8,6) | BOARD_BIT(8,7) | BOARD_BIT(8,8) )

/* all of the bits in column 8 */
#define COL8 ( BOARD_BIT(1,8) | BOARD_BIT(2,8) | BOARD_BIT(3,8) | BOARD_BIT(4,8) | \
               BOARD_BIT(5,8) | BOARD_BIT(6,8) | BOARD_BIT(7,8) | BOARD_BIT(8,8) )

/* all of the bits in column 1 */
#define COL1 (COL8 << 7)

#define IS_MOVE_OFF_BOARD(m) ( (m.row < 1) || (m.row > 8) || (m.col < 1) || (m.col > 8) )
#define IS_DIAGONAL_MOVE(m) ( (m.row != 0) && (m.col != 0) )
#define MOVE_OFFSET_TO_BIT_OFFSET(m) ( (m.row * 8) + (m.col) )


typedef unsigned long long ull;

/* 
	game board represented as a pair of bit vectors: 
	- one for x_black disks on the board
	- one for o_white disks on the board
*/

typedef struct { ull disks[2]; } Board;

typedef struct { int row; int col; } Move;


Move offsets[] = {
  {0,1}		/* right */,		{0,-1}		/* left */, 
  {-1,0}	/* up */,		{1,0}		/* down */, 
  {-1,-1}	/* up-left */,		{-1,1}		/* up-right */, 
  {1,1}		/* down-right */,	{1,-1}		/* down-left */
};

int noffsets = sizeof(offsets)/sizeof(Move);
char diskcolor[] = { '.', 'X', 'O', 'I' };


Board start = { 
	BOARD_BIT(4,5) | BOARD_BIT(5,4) /* X_BLACK */, 
	BOARD_BIT(4,4) | BOARD_BIT(5,5) /* O_WHITE */
};


void PrintBoard(Board b);
int HumanTurn(Board *b, int color);
int CountBitsOnBoard(const Board *b, int color);
void EndGame(Board b);


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

/* 
	place a disk of color at the position specified by m.row and m,col,
	flipping the opponents disk there (if any) 
*/

static void PlaceOrFlip(Move m, Board *b, int color) {
    ull bit = MOVE_TO_BOARD_BIT(m);
    b->disks[color] |= bit;
    b->disks[OTHERCOLOR(color)] &= ~bit;
}

/* 
	try to flip disks along a direction specified by a move offset.
	the return code is 0 if no flips were done.
	the return value is 1 + the number of flips otherwise.
*/

static int TryFlips(Move m, Move offset, Board *b, int color, int verbose, int domove) {
    Move next;
    next.row = m.row + offset.row;
    next.col = m.col + offset.col;

    if (!IS_MOVE_OFF_BOARD(next)) {
        ull nextbit = MOVE_TO_BOARD_BIT(next);
        if (nextbit & b->disks[OTHERCOLOR(color)]) {
            int nflips = TryFlips(next, offset, b, color, verbose, domove);
            if (nflips) {
                if (verbose) {
                    printf("flipping disk at %d,%d\n", next.row, next.col);
                }
                if (domove) PlaceOrFlip(next, b, color);
                return nflips + 1;
            }
        } else if (nextbit & b->disks[color]) {
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
        // If flipresult > 0 => it includes +1
        if (flipresult > 0) {
            nflips += (flipresult - 1);
        }
    }
    return nflips;
}


void ReadMove(int color, Board *b) {
    Move m;
    ull movebit;
    for (;;) {
        printf("Enter %c's move as 'row,col': ", diskcolor[color+1]);
        scanf("%d,%d", &m.row, &m.col);

        /* if move is not on the board, move again */
        if (IS_MOVE_OFF_BOARD(m)) {
            printf("Illegal move: row and column must both be between 1 and 8\n");
            PrintBoard(*b);
            continue;
        }
        movebit = MOVE_TO_BOARD_BIT(m);

        /* if board position occupied, move again */
        if (movebit & (b->disks[X_BLACK] | b->disks[O_WHITE])) {
            printf("Illegal move: board position already occupied.\n");
            PrintBoard(*b);
            continue;
        }

        /* if no disks have been flipped */ 
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
        break;
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

/*
	return the set of board positions adjacent to an opponent's
	disk that are empty. these represent a candidate set of 
	positions for a move by color.
*/

static Board NeighborMoves(Board b, int color) {
    Board neighbors = {0ULL, 0ULL};
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
    neighbors.disks[color] &= ~(b.disks[X_BLACK] | b.disks[O_WHITE]);
    return neighbors;
}

int EnumerateLegalMoves(Board b, int color, Board *legal_moves) {
    legal_moves->disks[color] = 0ULL;
    Board neighbors = NeighborMoves(b, color);
    ull my_neighbor_moves = neighbors.disks[color];
    int num_moves = 0;

    while (my_neighbor_moves) {
        ull next_move = my_neighbor_moves & -my_neighbor_moves;
        int bitpos = __builtin_ctzll(next_move);
        int row = 8 - (bitpos / 8);
        int col = 8 - (bitpos % 8);
        Move m = { row, col };
        if (FlipDisks(m, &b, color, 0, 0) > 0) {
            legal_moves->disks[color] |= next_move;
            num_moves++;
        }
        my_neighbor_moves ^= next_move;
    }
    return num_moves;
}


int CountBitsOnBoard(const Board *b, int color) {
    ull bits = b->disks[color];
    int ndisks = 0;
    while (bits) {
        bits &= (bits - 1); // clear the least significant bit set
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


// Check if neither side can move
bool GameIsOver(const Board &b) {
    Board dummy;
    int xMoves = EnumerateLegalMoves(b, X_BLACK, &dummy);
    int oMoves = EnumerateLegalMoves(b, O_WHITE, &dummy);
    return (xMoves == 0 && oMoves == 0);
}

// Evaluate the board
int EvaluateBoard(const Board &b, int color) {
    int myCount  = CountBitsOnBoard(&b, color);
    int oppCount = CountBitsOnBoard(&b, OTHERCOLOR(color));
    // If color==X_BLACK => score = myCount - oppCount
    // If color==O_WHITE => score = myCount - oppCount
    return (myCount - oppCount);
}

// Copy oldBoard
static int MakeMove(const Board *oldBoard, int color, Move m, Board *newBoard) {
    *newBoard = *oldBoard;  
    int flipped = FlipDisks(m, newBoard, color, 0, 1);
    PlaceOrFlip(m, newBoard, color);
    return flipped;
}


// Parallel Negamax
// Basic parallel Negamax return the best score for color
// depth is how many moves ahead to explore

int Negamax(const Board &b, int color, int depth) {
    if (depth == 0 || GameIsOver(b)) {
        return EvaluateBoard(b, color);
    }

    Board legalMoves;
    int numMoves = EnumerateLegalMoves(b, color, &legalMoves);
    if (numMoves == 0) {
        return -Negamax(b, OTHERCOLOR(color), depth - 1);
    }

    // Use serial execution for depths below the cutoff
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



// Define a "root" function that enumerates moves, sand then picks the best index.
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

// Computer Turn
int ComputerTurn(Board *b, int color, int depth) {
    // Check if there's a legal move
    Board legal;
    int num_moves = EnumerateLegalMoves(*b, color, &legal);
    if (num_moves == 0) {
        return 0;
    }

    Move bestM;
    int bestScore = NegamaxRoot(*b, color, depth, &bestM);

    printf("\n[%c] Computer chooses move (%d, %d) => Score: %d\n",
           (color==X_BLACK ? 'X':'O'), bestM.row, bestM.col, bestScore);

    int flips = FlipDisks(bestM, b, color, 1, 1);
    PlaceOrFlip(bestM, b, color);

    printf("%c flipped %d disks.\n", (color==X_BLACK ? 'X':'O'), flips);
    PrintBoard(*b);
    return 1; 
}


// Main
int main() {
    Board gameboard = start;
    PrintBoard(gameboard);

    char p1type, p2type;
    int p1depth = 0, p2depth = 0;

    printf("Is Player 1 (X) [h]uman or [c]omputer? ");
    scanf(" %c", &p1type);
    if (p1type == 'c') {
        printf("Enter search depth for X (1..60): ");
        scanf("%d", &p1depth);
    }

    printf("Is Player 2 (O) [h]uman or [c]omputer? ");
    scanf(" %c", &p2type);
    if (p2type == 'c') {
        printf("Enter search depth for O (1..60): ");
        scanf("%d", &p2depth);
    }

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

        if (!movePossibleX && !movePossibleO) {
            break;
        }


        currentColor = 1 - currentColor; 
    }

    EndGame(gameboard);
    return 0;
}

