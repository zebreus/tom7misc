
#ifndef _GRAD_PLAYER_H
#define _GRAD_PLAYER_H

#include <string>

struct Player;

Player *GradEval(const std::string &name, const std::string &model_file);
// Patches checkmate and draw detection.
Player *GradEvalFix(
    const std::string &name, const std::string &model_file);

#endif
