// Generado por tools/make_controllers.py: posición de cada botón en las ilustraciones
// assets/ui/pad_xbox.png y pad_ps.png (1200x760). No editar a mano.
#pragma once

enum class PadPart { A, B, X, Y, LB, RB, LT, RT, LS, RS, DPAD, BACK, START, GUIDE, Count };

constexpr float kPadImageW = 1200, kPadImageH = 760;
constexpr float kPadXboxAnchors[][2] = {
  {870, 362}, // A
  {932, 300}, // B
  {808, 300}, // X
  {870, 238}, // Y
  {330, 173}, // LB
  {870, 173}, // RB
  {330, 110}, // LT
  {870, 110}, // RT
  {330, 300}, // LS
  {745, 440}, // RS
  {455, 440}, // DPAD
  {525, 300}, // BACK
  {675, 300}, // START
  {600, 232}, // GUIDE
};
constexpr float kPadPsAnchors[][2] = {
  {870, 362}, // A
  {932, 300}, // B
  {808, 300}, // X
  {870, 238}, // Y
  {330, 173}, // LB
  {870, 173}, // RB
  {330, 110}, // LT
  {870, 110}, // RT
  {470, 450}, // LS
  {730, 450}, // RS
  {330, 300}, // DPAD
  {505, 262}, // BACK
  {695, 262}, // START
  {600, 360}, // GUIDE
};
