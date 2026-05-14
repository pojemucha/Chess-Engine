#include "Position.h"

namespace MyChess {

    // EvalWeights definitions
    short weight[12] = {100, 320, 330, 500, 900, 0, -100, -320, -330, -500, -900, 0};
    short PST_Midgame[12][64] = {{0}};
    short PST_Endgame[12][64] = {{0}};

}