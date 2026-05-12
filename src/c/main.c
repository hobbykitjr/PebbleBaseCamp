/**
 * Base Camp - Daily outdoor puzzle games for Pebble
 * Targets: emery (Time 2), gabbro (Round 2)
 *
 * Games: Tents & Trees, Twilight (Sun & Moon), Smoke Signal (Thermometer)
 * Features: Easy/Hard, streak tracking, tutorials, yesterday replay
 */

#include <pebble.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// CONSTANTS
// ============================================================================
#define NUM_GAMES 3
#define MAX_GRID  8
#define MAX_THERMOS 16  // max thermometer segments per puzzle

// States
enum {
  ST_MAIN_MENU, ST_DIFF_MENU, ST_TUTORIAL, ST_STATS,
  ST_TN_PLAY, ST_TN_CHECK, ST_TN_WIN,
  ST_BN_PLAY, ST_BN_CHECK, ST_BN_WIN,
  ST_SM_PLAY, ST_SM_CHECK, ST_SM_WIN,
};

// Games
enum { GAME_TENTS, GAME_BINAIRO, GAME_SMOKE };

// Tents cell values
#define TN_EMPTY 0
#define TN_TREE  1
#define TN_TENT  2
#define TN_GRASS 3

// Binairo cell values
#define BN_EMPTY 0
#define BN_SUN   1
#define BN_MOON  2

// Smoke Signal cell values
#define SM_EMPTY  0
#define SM_FILLED 1  // smoke/fire filled

// Font Awesome UTF-8 icon strings
#define FA_TREE      "\xEF\x86\xBB"  // U+F1BB tree
#define FA_CARET_UP  "\xEF\x83\x98"  // U+F0D8 tent marker
#define FA_CIRCLE    "\xEF\x84\x91"  // U+F111 grass dot
#define FA_SUN       "\xEF\x86\x85"  // U+F185 sun-o
#define FA_MOON      "\xEF\x86\x86"  // U+F186 moon-o
#define FA_FIRE      "\xEF\x81\xAD"  // U+F06D fire
#define FA_CLOUD     "\xEF\x83\x82"  // U+F0C2 cloud (smoke)

// ============================================================================
// PERSISTENCE KEYS
// ============================================================================
#define P_LAST_DAY       0
#define P_STREAK         1
#define P_BEST_STREAK    2
#define P_TOTAL_DAYS     3
#define P_TUTORIAL_SEEN  4

#define P_TN_BASE       10
#define P_BN_BASE       20
#define P_SM_BASE       70
#define PG_DONE_DAY_E    0
#define PG_DONE_DAY_H    1
#define PG_WINS_EASY     2
#define PG_WINS_HARD     3
#define PG_BEST_SCORE_E  4
#define PG_BEST_SCORE_H  5

// Save/resume keys
#define P_TN_SAVE_DAY   30
#define P_TN_SAVE_DIFF  31
#define P_TN_SAVE_DATA  32  // persist_write_data blob
#define P_BN_SAVE_DAY   40
#define P_BN_SAVE_DIFF  41
#define P_BN_SAVE_DATA  42  // persist_write_data blob
#define P_SM_SAVE_DAY   50
#define P_SM_SAVE_DIFF  51
#define P_SM_SAVE_DATA  52  // persist_write_data blob

static int game_base(int g) {
  if(g == GAME_BINAIRO) return P_BN_BASE;
  if(g == GAME_SMOKE) return P_SM_BASE;
  return P_TN_BASE;
}

// ============================================================================
// GLOBALS
// ============================================================================
static Window *s_win;
static Layer  *s_canvas;
static GFont   s_icon_font_20, s_icon_font_14;

static int s_state = ST_MAIN_MENU;
static int s_game = GAME_TENTS;
static int s_difficulty = 0;
static int s_yesterday = 0;
static int s_menu_cursor = 0;
static int s_diff_cursor = 0;
static int s_tut_page = 0;

static int s_last_day = 0;
static int s_streak = 0;
static int s_best_streak = 0;
static int s_total_days = 0;
static int s_tutorial_seen = 0;

// ============================================================================
// DAILY SEED
// ============================================================================
static int day_number(void) {
  return (int)(time(NULL) / 86400);
}

static void seed_game(int game, int diff, int yest) {
  int day = day_number() - yest;
  srand(day * 1000 + game * 100 + diff * 10 + 1);
}

// ============================================================================
// COMPLETION TRACKING
// ============================================================================
static bool game_done_today(int game, int diff) {
  int key = game_base(game) + (diff ? PG_DONE_DAY_H : PG_DONE_DAY_E);
  return persist_exists(key) && persist_read_int(key) == day_number();
}

static bool game_done_any_today(int game) {
  return game_done_today(game, 0) || game_done_today(game, 1);
}

static void mark_game_done(int game, int diff, int score) {
  int base = game_base(game);
  int today = day_number();
  persist_write_int(base + (diff ? PG_DONE_DAY_H : PG_DONE_DAY_E), today);
  int wkey = base + (diff ? PG_WINS_HARD : PG_WINS_EASY);
  int wins = persist_exists(wkey) ? persist_read_int(wkey) : 0;
  persist_write_int(wkey, wins + 1);
  int bkey = base + (diff ? PG_BEST_SCORE_H : PG_BEST_SCORE_E);
  int best = persist_exists(bkey) ? persist_read_int(bkey) : 0;
  if(best == 0 || score < best) persist_write_int(bkey, score);
  if(s_last_day < today) {
    s_streak = (s_last_day == today - 1) ? s_streak + 1 : 1;
    s_last_day = today;
    s_total_days++;
    persist_write_int(P_LAST_DAY, s_last_day);
    persist_write_int(P_STREAK, s_streak);
    persist_write_int(P_TOTAL_DAYS, s_total_days);
    if(s_streak > s_best_streak) {
      s_best_streak = s_streak;
      persist_write_int(P_BEST_STREAK, s_best_streak);
    }
  }
}

// ============================================================================
// GRID CURSOR (shared by both games)
// ============================================================================
static int g_rows, g_cols;
static int g_cursor_r, g_cursor_c;
static bool g_locked[MAX_GRID][MAX_GRID];

static void grid_skip_locked(int dr, int dc) {
  for(int i = 0; i < g_rows * g_cols; i++) {
    if(!g_locked[g_cursor_r][g_cursor_c]) return;
    if(dc != 0) {
      g_cursor_c += dc;
      if(g_cursor_c >= g_cols) { g_cursor_c = 0; g_cursor_r = (g_cursor_r + 1) % g_rows; }
      if(g_cursor_c < 0) { g_cursor_c = g_cols - 1; g_cursor_r = (g_cursor_r + g_rows - 1) % g_rows; }
    } else {
      g_cursor_r = (g_cursor_r + dr + g_rows) % g_rows;
    }
  }
}

static void grid_move_up(void) {
  g_cursor_r = (g_cursor_r + g_rows - 1) % g_rows;
  grid_skip_locked(-1, 0);
}

static void grid_move_down(void) {
  g_cursor_r = (g_cursor_r + 1) % g_rows;
  grid_skip_locked(1, 0);
}

static void grid_move_right_stay_row(void) {
  int orig_r = g_cursor_r, orig_c = g_cursor_c;
  g_cursor_c = (g_cursor_c + 1) % g_cols;
  // Skip locked in row, if we wrap back to start, go next row
  for(int i = 0; i < g_cols; i++) {
    if(!g_locked[g_cursor_r][g_cursor_c]) {
      if(g_cursor_r != orig_r || g_cursor_c != orig_c) return;
      break;
    }
    g_cursor_c = (g_cursor_c + 1) % g_cols;
  }
  g_cursor_r = (g_cursor_r + 1) % g_rows;
  g_cursor_c = 0;
  grid_skip_locked(0, 1);
}

static void grid_move_left(void) {
  g_cursor_c--;
  if(g_cursor_c < 0) { g_cursor_c = g_cols - 1; g_cursor_r = (g_cursor_r + g_rows - 1) % g_rows; }
  grid_skip_locked(0, -1);
}

static void grid_cursor_to_first_unlocked(void) {
  for(int r = 0; r < g_rows; r++)
    for(int c = 0; c < g_cols; c++)
      if(!g_locked[r][c]) { g_cursor_r = r; g_cursor_c = c; return; }
}

// ============================================================================
// SAVE / RESTORE PROGRESS
// ============================================================================
// Board cells packed as uint8_t[64] (row-major, val 0-3)
static void _save_keys(int game_id, int *day_key, int *diff_key, int *data_key) {
  if(game_id == GAME_SMOKE)   { *day_key=P_SM_SAVE_DAY; *diff_key=P_SM_SAVE_DIFF; *data_key=P_SM_SAVE_DATA; }
  else if(game_id == GAME_BINAIRO) { *day_key=P_BN_SAVE_DAY; *diff_key=P_BN_SAVE_DIFF; *data_key=P_BN_SAVE_DATA; }
  else { *day_key=P_TN_SAVE_DAY; *diff_key=P_TN_SAVE_DIFF; *data_key=P_TN_SAVE_DATA; }
}

static void save_game_progress(int game_id, int board[MAX_GRID][MAX_GRID], int size) {
  if(s_yesterday) return;
  int day_key, diff_key, data_key;
  _save_keys(game_id, &day_key, &diff_key, &data_key);
  persist_write_int(day_key, day_number());
  persist_write_int(diff_key, s_difficulty);
  uint8_t buf[64];
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++)
      buf[r * size + c] = (uint8_t)(g_locked[r][c] ? 0xFF : board[r][c]);
  persist_write_data(data_key, buf, size * size);
}

static bool restore_game_progress(int game_id, int board[MAX_GRID][MAX_GRID], int size) {
  if(s_yesterday) return false;
  int day_key, diff_key, data_key;
  _save_keys(game_id, &day_key, &diff_key, &data_key);
  if(!persist_exists(day_key) || persist_read_int(day_key) != day_number()) return false;
  if(!persist_exists(diff_key) || persist_read_int(diff_key) != s_difficulty) return false;
  uint8_t buf[64];
  if(persist_read_data(data_key, buf, size * size) != (int)(size * size)) return false;
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++) {
      if(buf[r * size + c] == 0xFF) continue;  // locked cell, keep as-is
      board[r][c] = buf[r * size + c];
    }
  return true;
}

// ============================================================================
// TENTS & TREES
// ============================================================================
static void tn_update_warnings(void);  // forward decl
static void bn_update_warnings(void);  // forward decl
static int tn_size;
static int tn_board[MAX_GRID][MAX_GRID];
static int tn_solution[MAX_GRID][MAX_GRID];
static int tn_row_clues[MAX_GRID];
static int tn_col_clues[MAX_GRID];
static bool tn_error[MAX_GRID][MAX_GRID];
static bool tn_warn[MAX_GRID][MAX_GRID];
static char tn_error_msg[24];

static void tn_generate(int size) {
  int target = (size == 6) ? (5 + rand() % 3) : (9 + rand() % 5);
  int best_pairs = 0;
  int best[MAX_GRID][MAX_GRID];
  memset(best, 0, sizeof(best));

  for(int attempt = 0; attempt < 80; attempt++) {
    int board[MAX_GRID][MAX_GRID];
    memset(board, 0, sizeof(board));
    int pairs = 0;

    // Shuffle cell order
    int cells[64][2], nc = 0;
    for(int r = 0; r < size; r++)
      for(int c = 0; c < size; c++) { cells[nc][0] = r; cells[nc][1] = c; nc++; }
    for(int i = nc - 1; i > 0; i--) {
      int j = rand() % (i + 1);
      int tr = cells[i][0], tc = cells[i][1];
      cells[i][0] = cells[j][0]; cells[i][1] = cells[j][1];
      cells[j][0] = tr; cells[j][1] = tc;
    }

    for(int ci = 0; ci < nc && pairs < target; ci++) {
      int r = cells[ci][0], c = cells[ci][1];
      if(board[r][c] != TN_EMPTY) continue;
      // Check no tent in 8-neighborhood
      bool adj_tent = false;
      for(int dr = -1; dr <= 1 && !adj_tent; dr++)
        for(int dc = -1; dc <= 1 && !adj_tent; dc++) {
          if(dr == 0 && dc == 0) continue;
          int nr = r + dr, nc2 = c + dc;
          if(nr >= 0 && nr < size && nc2 >= 0 && nc2 < size && board[nr][nc2] == TN_TENT)
            adj_tent = true;
        }
      if(adj_tent) continue;

      // Find empty orthogonal neighbor for tree
      int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
      // Shuffle dirs
      for(int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        int t0 = dirs[i][0], t1 = dirs[i][1];
        dirs[i][0] = dirs[j][0]; dirs[i][1] = dirs[j][1];
        dirs[j][0] = t0; dirs[j][1] = t1;
      }
      for(int d = 0; d < 4; d++) {
        int tr = r + dirs[d][0], tc = c + dirs[d][1];
        if(tr >= 0 && tr < size && tc >= 0 && tc < size && board[tr][tc] == TN_EMPTY) {
          board[r][c] = TN_TENT;
          board[tr][tc] = TN_TREE;
          pairs++;
          break;
        }
      }
    }

    if(pairs > best_pairs) {
      best_pairs = pairs;
      memcpy(best, board, sizeof(board));
    }
    if(pairs >= target) break;
  }

  // Build solution, clues, puzzle
  memcpy(tn_solution, best, sizeof(tn_solution));
  for(int r = 0; r < size; r++) {
    tn_row_clues[r] = 0;
    for(int c = 0; c < size; c++)
      if(best[r][c] == TN_TENT) tn_row_clues[r]++;
  }
  for(int c = 0; c < size; c++) {
    tn_col_clues[c] = 0;
    for(int r = 0; r < size; r++)
      if(best[r][c] == TN_TENT) tn_col_clues[c]++;
  }
  // Puzzle: keep trees only
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++)
      tn_board[r][c] = (best[r][c] == TN_TREE) ? TN_TREE : TN_EMPTY;
}

static void tn_init(void) {
  tn_size = (s_difficulty == 0) ? 6 : 8;
  seed_game(GAME_TENTS, s_difficulty, s_yesterday);
  tn_generate(tn_size);
  g_rows = g_cols = tn_size;
  memset(g_locked, 0, sizeof(g_locked));
  for(int r = 0; r < tn_size; r++)
    for(int c = 0; c < tn_size; c++)
      g_locked[r][c] = (tn_board[r][c] == TN_TREE);
  grid_cursor_to_first_unlocked();
  restore_game_progress(GAME_TENTS, tn_board, tn_size);
  memset(tn_error, 0, sizeof(tn_error));
  memset(tn_warn, 0, sizeof(tn_warn));
  tn_error_msg[0] = '\0';
  tn_update_warnings();
  s_state = ST_TN_PLAY;
}

static void tn_update_warnings(void) {
  memset(tn_warn, 0, sizeof(tn_warn));
  int size = tn_size;
  // Adjacent tents (8-connected)
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++) {
      if(tn_board[r][c] != TN_TENT) continue;
      for(int r2 = r; r2 < size; r2++)
        for(int c2 = (r2 == r ? c + 1 : 0); c2 < size; c2++) {
          if(tn_board[r2][c2] != TN_TENT) continue;
          if(abs(r - r2) <= 1 && abs(c - c2) <= 1) {
            tn_warn[r][c] = true; tn_warn[r2][c2] = true;
          }
        }
    }
  // Tent not adjacent to any tree
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++) {
      if(tn_board[r][c] != TN_TENT) continue;
      bool has_tree = false;
      int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
      for(int d = 0; d < 4; d++) {
        int nr = r + dirs[d][0], nc = c + dirs[d][1];
        if(nr >= 0 && nr < size && nc >= 0 && nc < size && tn_board[nr][nc] == TN_TREE)
          has_tree = true;
      }
      if(!has_tree) tn_warn[r][c] = true;
    }
  // Row/col overcount
  for(int r = 0; r < size; r++) {
    int cnt = 0;
    for(int c = 0; c < size; c++) if(tn_board[r][c] == TN_TENT) cnt++;
    if(cnt > tn_row_clues[r])
      for(int c = 0; c < size; c++) if(tn_board[r][c] == TN_TENT) tn_warn[r][c] = true;
  }
  for(int c = 0; c < size; c++) {
    int cnt = 0;
    for(int r = 0; r < size; r++) if(tn_board[r][c] == TN_TENT) cnt++;
    if(cnt > tn_col_clues[c])
      for(int r = 0; r < size; r++) if(tn_board[r][c] == TN_TENT) tn_warn[r][c] = true;
  }
}

// Tent-tree matching via backtracking
static bool tn_match_solve(int ti, int n_tents, int tents[][2], int n_trees, int trees[][2], bool *used, int size) {
  if(ti == n_tents) return true;
  int tr = tents[ti][0], tc = tents[ti][1];
  for(int j = 0; j < n_trees; j++) {
    if(used[j]) continue;
    if(abs(tr - trees[j][0]) + abs(tc - trees[j][1]) == 1) {
      used[j] = true;
      if(tn_match_solve(ti + 1, n_tents, tents, n_trees, trees, used, size)) return true;
      used[j] = false;
    }
  }
  return false;
}

static bool tn_check_solution(void) {
  int size = tn_size;
  memset(tn_error, 0, sizeof(tn_error));
  bool has_err = false;

  // Row counts
  for(int r = 0; r < size; r++) {
    int cnt = 0;
    for(int c = 0; c < size; c++) if(tn_board[r][c] == TN_TENT) cnt++;
    if(cnt != tn_row_clues[r]) {
      for(int c = 0; c < size; c++) if(tn_board[r][c] != TN_TREE) tn_error[r][c] = true;
      has_err = true;
    }
  }
  for(int c = 0; c < size; c++) {
    int cnt = 0;
    for(int r = 0; r < size; r++) if(tn_board[r][c] == TN_TENT) cnt++;
    if(cnt != tn_col_clues[c]) {
      for(int r = 0; r < size; r++) if(tn_board[r][c] != TN_TREE) tn_error[r][c] = true;
      has_err = true;
    }
  }
  if(has_err) { strncpy(tn_error_msg, "Count mismatch!", sizeof(tn_error_msg)); return false; }

  // Adjacent tents
  int tents[32][2], n_tents = 0;
  int trees[32][2], n_trees = 0;
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++) {
      if(tn_board[r][c] == TN_TENT) { tents[n_tents][0] = r; tents[n_tents][1] = c; n_tents++; }
      if(tn_board[r][c] == TN_TREE) { trees[n_trees][0] = r; trees[n_trees][1] = c; n_trees++; }
    }
  for(int i = 0; i < n_tents; i++)
    for(int j = i + 1; j < n_tents; j++)
      if(abs(tents[i][0]-tents[j][0]) <= 1 && abs(tents[i][1]-tents[j][1]) <= 1) {
        tn_error[tents[i][0]][tents[i][1]] = true;
        tn_error[tents[j][0]][tents[j][1]] = true;
        has_err = true;
      }
  if(has_err) { strncpy(tn_error_msg, "Tents touching!", sizeof(tn_error_msg)); return false; }

  if(n_tents != n_trees) { strncpy(tn_error_msg, "Wrong tent count!", sizeof(tn_error_msg)); return false; }

  // Matching
  bool used[32];
  memset(used, 0, sizeof(used));
  if(!tn_match_solve(0, n_tents, tents, n_trees, trees, used, size)) {
    for(int i = 0; i < n_tents; i++) tn_error[tents[i][0]][tents[i][1]] = true;
    strncpy(tn_error_msg, "Bad pairing!", sizeof(tn_error_msg));
    return false;
  }
  return true;
}

static void tn_toggle_and_check(void) {
  if(g_locked[g_cursor_r][g_cursor_c]) return;
  int *cell = &tn_board[g_cursor_r][g_cursor_c];
  if(*cell == TN_EMPTY) *cell = TN_TENT;
  else if(*cell == TN_TENT) *cell = TN_GRASS;
  else *cell = TN_EMPTY;
  tn_update_warnings();

  // Auto-check when tent count matches
  int total_exp = 0, total_placed = 0;
  for(int r = 0; r < tn_size; r++) total_exp += tn_row_clues[r];
  for(int r = 0; r < tn_size; r++)
    for(int c = 0; c < tn_size; c++)
      if(tn_board[r][c] == TN_TENT) total_placed++;
  if(total_placed == total_exp) {
    if(tn_check_solution()) {
      if(!s_yesterday) mark_game_done(GAME_TENTS, s_difficulty, 1);
      s_state = ST_TN_WIN; vibes_short_pulse();
    } else {
      s_state = ST_TN_CHECK;
    }
  }
}

// ============================================================================
// BINAIRO
// ============================================================================
static int bn_size;
static int bn_board[MAX_GRID][MAX_GRID];
static int bn_solution[MAX_GRID][MAX_GRID];
static bool bn_error[MAX_GRID][MAX_GRID];
static bool bn_warn[MAX_GRID][MAX_GRID];
static char bn_error_msg[24];

static bool bn_valid_partial(int board[MAX_GRID][MAX_GRID], int size, int r, int c, int val) {
  board[r][c] = val;
  // No 3 consecutive in row
  if(c >= 2 && board[r][c-1] == val && board[r][c-2] == val) { board[r][c] = 0; return false; }
  if(c >= 1 && c < size-1 && board[r][c-1] == val && board[r][c+1] == val) { board[r][c] = 0; return false; }
  if(c < size-2 && board[r][c+1] == val && board[r][c+2] == val) { board[r][c] = 0; return false; }
  // No 3 consecutive in col
  if(r >= 2 && board[r-1][c] == val && board[r-2][c] == val) { board[r][c] = 0; return false; }
  if(r >= 1 && r < size-1 && board[r-1][c] == val && board[r+1][c] == val) { board[r][c] = 0; return false; }
  if(r < size-2 && board[r+1][c] == val && board[r+2][c] == val) { board[r][c] = 0; return false; }
  // Count constraint
  int half = size / 2;
  int row_cnt = 0, col_cnt = 0;
  for(int i = 0; i < size; i++) { if(board[r][i] == val) row_cnt++; if(board[i][c] == val) col_cnt++; }
  if(row_cnt > half || col_cnt > half) { board[r][c] = 0; return false; }
  board[r][c] = 0;
  return true;
}

// Iterative backtracking solver (recursive version blows Pebble's stack)
static int  bn_first_val[64]; // which value to try first per cell
static int8_t bn_tried[64];  // 0=untried, 1=tried first, 2=tried both

static bool bn_rows_unique(int board[MAX_GRID][MAX_GRID], int size) {
  for(int r1 = 0; r1 < size; r1++)
    for(int r2 = r1 + 1; r2 < size; r2++) {
      bool same = true;
      for(int c = 0; c < size && same; c++) if(board[r1][c] != board[r2][c]) same = false;
      if(same) return false;
    }
  for(int c1 = 0; c1 < size; c1++)
    for(int c2 = c1 + 1; c2 < size; c2++) {
      bool same = true;
      for(int r = 0; r < size && same; r++) if(board[r][c1] != board[r][c2]) same = false;
      if(same) return false;
    }
  return true;
}

static bool bn_solve_iterative(int board[MAX_GRID][MAX_GRID], int size) {
  int total = size * size;
  for(int i = 0; i < total; i++) {
    bn_first_val[i] = (rand() % 2) ? BN_SUN : BN_MOON;
    bn_tried[i] = 0;
  }
  int idx = 0;
  while(idx >= 0 && idx < total) {
    int r = idx / size, c = idx % size;
    int v1 = bn_first_val[idx];
    int v2 = (v1 == BN_SUN) ? BN_MOON : BN_SUN;
    bool placed = false;
    if(bn_tried[idx] == 0) {
      bn_tried[idx] = 1;
      if(bn_valid_partial(board, size, r, c, v1)) {
        board[r][c] = v1; placed = true;
      }
    }
    if(!placed && bn_tried[idx] <= 1) {
      bn_tried[idx] = 2;
      if(bn_valid_partial(board, size, r, c, v2)) {
        board[r][c] = v2; placed = true;
      }
    }
    if(placed) {
      idx++;
    } else {
      // Backtrack
      board[r][c] = 0;
      bn_tried[idx] = 0;
      idx--;
      if(idx >= 0) board[idx / size][idx % size] = 0;
    }
  }
  if(idx < 0) return false;
  return bn_rows_unique(board, size);
}

static void bn_generate(int size) {
  memset(bn_solution, 0, sizeof(bn_solution));
  // Try solving; if uniqueness fails, retry with offset
  for(int attempt = 0; attempt < 10; attempt++) {
    memset(bn_solution, 0, sizeof(bn_solution));
    if(attempt > 0) srand(rand() + attempt);
    if(bn_solve_iterative(bn_solution, size))
      break;
  }

  // Create puzzle: copy solution then remove cells
  memcpy(bn_board, bn_solution, sizeof(bn_board));
  memset(g_locked, 0, sizeof(g_locked));
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++)
      g_locked[r][c] = true;

  int total = size * size;
  int clue_ratio_pct = (size == 6) ? 55 : 45;
  int target_empty = total * (100 - clue_ratio_pct) / 100;

  // Build shuffled index array for removal
  int8_t order[64];
  for(int i = 0; i < total; i++) order[i] = (int8_t)i;
  for(int i = total - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int8_t tmp = order[i]; order[i] = order[j]; order[j] = tmp;
  }
  for(int i = 0; i < target_empty && i < total; i++) {
    int idx = order[i];
    int r = idx / size, c = idx % size;
    bn_board[r][c] = BN_EMPTY;
    g_locked[r][c] = false;
  }
}

static void bn_init(void) {
  bn_size = (s_difficulty == 0) ? 6 : 8;
  seed_game(GAME_BINAIRO, s_difficulty, s_yesterday);
  bn_generate(bn_size);
  g_rows = g_cols = bn_size;
  grid_cursor_to_first_unlocked();
  restore_game_progress(GAME_BINAIRO, bn_board, bn_size);
  memset(bn_error, 0, sizeof(bn_error));
  memset(bn_warn, 0, sizeof(bn_warn));
  bn_error_msg[0] = '\0';
  bn_update_warnings();
  s_state = ST_BN_PLAY;
}

static void bn_update_warnings(void) {
  memset(bn_warn, 0, sizeof(bn_warn));
  int size = bn_size;
  // 3 consecutive in rows
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size - 2; c++)
      if(bn_board[r][c] != BN_EMPTY && bn_board[r][c] == bn_board[r][c+1] && bn_board[r][c] == bn_board[r][c+2])
        { bn_warn[r][c] = bn_warn[r][c+1] = bn_warn[r][c+2] = true; }
  // 3 consecutive in cols
  for(int c = 0; c < size; c++)
    for(int r = 0; r < size - 2; r++)
      if(bn_board[r][c] != BN_EMPTY && bn_board[r][c] == bn_board[r+1][c] && bn_board[r][c] == bn_board[r+2][c])
        { bn_warn[r][c] = bn_warn[r+1][c] = bn_warn[r+2][c] = true; }
  // Too many of one symbol
  int half = size / 2;
  for(int r = 0; r < size; r++) {
    int suns = 0, moons = 0;
    for(int c = 0; c < size; c++) { if(bn_board[r][c] == BN_SUN) suns++; if(bn_board[r][c] == BN_MOON) moons++; }
    if(suns > half || moons > half)
      for(int c = 0; c < size; c++) if(bn_board[r][c] != BN_EMPTY) bn_warn[r][c] = true;
  }
  for(int c = 0; c < size; c++) {
    int suns = 0, moons = 0;
    for(int r = 0; r < size; r++) { if(bn_board[r][c] == BN_SUN) suns++; if(bn_board[r][c] == BN_MOON) moons++; }
    if(suns > half || moons > half)
      for(int r = 0; r < size; r++) if(bn_board[r][c] != BN_EMPTY) bn_warn[r][c] = true;
  }
}

static bool bn_check_solution(void) {
  int size = bn_size;
  memset(bn_error, 0, sizeof(bn_error));
  bool has_err = false;

  for(int r = 0; r < size; r++)
    for(int c = 0; c < size; c++)
      if(bn_board[r][c] == BN_EMPTY) { strncpy(bn_error_msg, "Fill all cells!", sizeof(bn_error_msg)); return false; }

  // 3 consecutive
  for(int r = 0; r < size; r++)
    for(int c = 0; c < size - 2; c++)
      if(bn_board[r][c] == bn_board[r][c+1] && bn_board[r][c] == bn_board[r][c+2])
        { bn_error[r][c] = bn_error[r][c+1] = bn_error[r][c+2] = true; has_err = true; }
  for(int c = 0; c < size; c++)
    for(int r = 0; r < size - 2; r++)
      if(bn_board[r][c] == bn_board[r+1][c] && bn_board[r][c] == bn_board[r+2][c])
        { bn_error[r][c] = bn_error[r+1][c] = bn_error[r+2][c] = true; has_err = true; }

  // Count check
  int half = size / 2;
  for(int r = 0; r < size; r++) {
    int suns = 0, moons = 0;
    for(int c = 0; c < size; c++) { if(bn_board[r][c] == BN_SUN) suns++; if(bn_board[r][c] == BN_MOON) moons++; }
    if(suns != half || moons != half)
      { for(int c = 0; c < size; c++) bn_error[r][c] = true; has_err = true; }
  }
  for(int c = 0; c < size; c++) {
    int suns = 0, moons = 0;
    for(int r = 0; r < size; r++) { if(bn_board[r][c] == BN_SUN) suns++; if(bn_board[r][c] == BN_MOON) moons++; }
    if(suns != half || moons != half)
      { for(int r = 0; r < size; r++) bn_error[r][c] = true; has_err = true; }
  }

  // Unique rows
  for(int r1 = 0; r1 < size; r1++)
    for(int r2 = r1 + 1; r2 < size; r2++) {
      bool same = true;
      for(int c = 0; c < size && same; c++) if(bn_board[r1][c] != bn_board[r2][c]) same = false;
      if(same) { for(int c = 0; c < size; c++) { bn_error[r1][c] = bn_error[r2][c] = true; } has_err = true; }
    }
  // Unique cols
  for(int c1 = 0; c1 < size; c1++)
    for(int c2 = c1 + 1; c2 < size; c2++) {
      bool same = true;
      for(int r = 0; r < size && same; r++) if(bn_board[r][c1] != bn_board[r][c2]) same = false;
      if(same) { for(int r = 0; r < size; r++) { bn_error[r][c1] = bn_error[r][c2] = true; } has_err = true; }
    }

  if(has_err) { strncpy(bn_error_msg, "Errors found!", sizeof(bn_error_msg)); return false; }
  return true;
}

static void bn_toggle_and_check(void) {
  if(g_locked[g_cursor_r][g_cursor_c]) return;
  int *cell = &bn_board[g_cursor_r][g_cursor_c];
  if(*cell == BN_EMPTY) *cell = BN_SUN;
  else if(*cell == BN_SUN) *cell = BN_MOON;
  else *cell = BN_EMPTY;
  bn_update_warnings();

  // Auto-check when full
  bool full = true;
  for(int r = 0; r < bn_size && full; r++)
    for(int c = 0; c < bn_size && full; c++)
      if(bn_board[r][c] == BN_EMPTY) full = false;
  if(full) {
    if(bn_check_solution()) {
      if(!s_yesterday) mark_game_done(GAME_BINAIRO, s_difficulty, 1);
      s_state = ST_BN_WIN; vibes_short_pulse();
    } else {
      s_state = ST_BN_CHECK;
    }
  }
}

// ============================================================================
// SMOKE SIGNAL (Thermometer puzzle)
// ============================================================================
static void sm_update_warnings(void);  // forward decl
static int sm_size;
static int sm_board[MAX_GRID][MAX_GRID];   // 0=empty, 1=filled
// Thermometer data: each thermo is a list of (r,c) from base to tip
static int sm_thermo_count;
static int sm_thermos[MAX_THERMOS][MAX_GRID]; // packed r*8+c per cell
static int sm_thermo_len[MAX_THERMOS];         // length of each thermo
static int sm_thermo_id[MAX_GRID][MAX_GRID];   // which thermo owns cell (-1 = none)
static int sm_thermo_pos[MAX_GRID][MAX_GRID];  // position in thermo (0=base)
static int sm_row_clues[MAX_GRID];
static int sm_col_clues[MAX_GRID];
static bool sm_error[MAX_GRID][MAX_GRID];
static bool sm_warn[MAX_GRID][MAX_GRID];
static char sm_error_msg[24];

static void sm_generate(int size) {
  // Place thermometers randomly on the grid
  memset(sm_thermo_id, -1, sizeof(sm_thermo_id));
  memset(sm_thermo_pos, 0, sizeof(sm_thermo_pos));
  sm_thermo_count = 0;

  int target = (size == 6) ? 6 : 10;  // target number of thermometers
  int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};  // up,down,left,right

  for(int attempt = 0; attempt < 200 && sm_thermo_count < target; attempt++) {
    int r = rand() % size, c = rand() % size;
    if(sm_thermo_id[r][c] >= 0) continue;
    // Pick random direction
    int di = rand() % 4;
    int dr = dirs[di][0], dc = dirs[di][1];
    // Determine max length in this direction
    int max_len = 1;
    for(int i = 1; i < size; i++) {
      int nr = r + dr * i, nc = c + dc * i;
      if(nr < 0 || nr >= size || nc < 0 || nc >= size) break;
      if(sm_thermo_id[nr][nc] >= 0) break;
      max_len++;
    }
    if(max_len < 2) continue;
    int len = 2 + rand() % (max_len - 1);  // 2 to max_len
    if(len > max_len) len = max_len;
    // Place thermometer
    int ti = sm_thermo_count;
    sm_thermo_len[ti] = len;
    for(int i = 0; i < len; i++) {
      int cr = r + dr * i, cc = c + dc * i;
      sm_thermos[ti][i] = cr * MAX_GRID + cc;
      sm_thermo_id[cr][cc] = ti;
      sm_thermo_pos[cr][cc] = i;
    }
    sm_thermo_count++;
  }

  // Generate solution: randomly fill each thermo from base up to some level
  int sol[MAX_GRID][MAX_GRID];
  memset(sol, 0, sizeof(sol));
  for(int ti = 0; ti < sm_thermo_count; ti++) {
    int fill = rand() % (sm_thermo_len[ti] + 1);  // 0 to full length
    for(int i = 0; i < fill; i++) {
      int packed = sm_thermos[ti][i];
      sol[packed / MAX_GRID][packed % MAX_GRID] = SM_FILLED;
    }
  }

  // Compute clues
  for(int r = 0; r < size; r++) {
    sm_row_clues[r] = 0;
    for(int c = 0; c < size; c++) if(sol[r][c] == SM_FILLED) sm_row_clues[r]++;
  }
  for(int c = 0; c < size; c++) {
    sm_col_clues[c] = 0;
    for(int r = 0; r < size; r++) if(sol[r][c] == SM_FILLED) sm_col_clues[c]++;
  }

  // Player board starts empty (thermos are visible but unfilled)
  memset(sm_board, 0, sizeof(sm_board));
}

static void sm_init(void) {
  sm_size = (s_difficulty == 0) ? 6 : 8;
  seed_game(GAME_SMOKE, s_difficulty, s_yesterday);
  sm_generate(sm_size);
  g_rows = g_cols = sm_size;
  // No cells are locked in smoke signal - all playable (except non-thermo cells)
  memset(g_locked, 0, sizeof(g_locked));
  for(int r = 0; r < sm_size; r++)
    for(int c = 0; c < sm_size; c++)
      g_locked[r][c] = (sm_thermo_id[r][c] < 0);  // lock non-thermo cells
  grid_cursor_to_first_unlocked();
  restore_game_progress(GAME_SMOKE, sm_board, sm_size);
  memset(sm_error, 0, sizeof(sm_error));
  memset(sm_warn, 0, sizeof(sm_warn));
  sm_error_msg[0] = '\0';
  sm_update_warnings();
  s_state = ST_SM_PLAY;
}

static void sm_update_warnings(void) {
  memset(sm_warn, 0, sizeof(sm_warn));
  int size = sm_size;
  // Warn if filled cell has a gap below it in same thermo
  for(int ti = 0; ti < sm_thermo_count; ti++) {
    bool gap = false;
    for(int i = 0; i < sm_thermo_len[ti]; i++) {
      int packed = sm_thermos[ti][i];
      int r = packed / MAX_GRID, c = packed % MAX_GRID;
      if(sm_board[r][c] == SM_FILLED && gap) sm_warn[r][c] = true;
      if(sm_board[r][c] == SM_EMPTY) gap = true;
    }
  }
  // Warn if row/col count exceeded
  for(int r = 0; r < size; r++) {
    int cnt = 0;
    for(int c = 0; c < size; c++) if(sm_board[r][c] == SM_FILLED) cnt++;
    if(cnt > sm_row_clues[r])
      for(int c = 0; c < size; c++) if(sm_board[r][c] == SM_FILLED) sm_warn[r][c] = true;
  }
  for(int c = 0; c < size; c++) {
    int cnt = 0;
    for(int r = 0; r < size; r++) if(sm_board[r][c] == SM_FILLED) cnt++;
    if(cnt > sm_col_clues[c])
      for(int r = 0; r < size; r++) if(sm_board[r][c] == SM_FILLED) sm_warn[r][c] = true;
  }
}

static bool sm_check_solution(void) {
  int size = sm_size;
  memset(sm_error, 0, sizeof(sm_error));
  bool has_err = false;

  // Check row counts
  for(int r = 0; r < size; r++) {
    int cnt = 0;
    for(int c = 0; c < size; c++) if(sm_board[r][c] == SM_FILLED) cnt++;
    if(cnt != sm_row_clues[r]) {
      for(int c = 0; c < size; c++) if(sm_thermo_id[r][c] >= 0) sm_error[r][c] = true;
      has_err = true;
    }
  }
  for(int c = 0; c < size; c++) {
    int cnt = 0;
    for(int r = 0; r < size; r++) if(sm_board[r][c] == SM_FILLED) cnt++;
    if(cnt != sm_col_clues[c]) {
      for(int r = 0; r < size; r++) if(sm_thermo_id[r][c] >= 0) sm_error[r][c] = true;
      has_err = true;
    }
  }
  if(has_err) { strncpy(sm_error_msg, "Count mismatch!", sizeof(sm_error_msg)); return false; }

  // Check continuity: filled cells must be contiguous from base
  for(int ti = 0; ti < sm_thermo_count; ti++) {
    bool gap = false;
    for(int i = 0; i < sm_thermo_len[ti]; i++) {
      int packed = sm_thermos[ti][i];
      int r = packed / MAX_GRID, c = packed % MAX_GRID;
      if(sm_board[r][c] == SM_FILLED && gap) {
        sm_error[r][c] = true; has_err = true;
      }
      if(sm_board[r][c] == SM_EMPTY) gap = true;
    }
  }
  if(has_err) { strncpy(sm_error_msg, "Smoke has gaps!", sizeof(sm_error_msg)); return false; }
  return true;
}

static void sm_toggle_and_check(void) {
  if(g_locked[g_cursor_r][g_cursor_c]) return;
  int *cell = &sm_board[g_cursor_r][g_cursor_c];
  *cell = (*cell == SM_EMPTY) ? SM_FILLED : SM_EMPTY;
  sm_update_warnings();

  // Auto-check when total filled matches total expected
  int total_exp = 0, total_placed = 0;
  for(int r = 0; r < sm_size; r++) total_exp += sm_row_clues[r];
  for(int r = 0; r < sm_size; r++)
    for(int c = 0; c < sm_size; c++)
      if(sm_board[r][c] == SM_FILLED) total_placed++;
  if(total_placed == total_exp) {
    if(sm_check_solution()) {
      if(!s_yesterday) mark_game_done(GAME_SMOKE, s_difficulty, 1);
      s_state = ST_SM_WIN; vibes_short_pulse();
    } else {
      s_state = ST_SM_CHECK;
    }
  }
}

// ============================================================================
// DRAWING HELPERS
// ============================================================================
static void draw_checkmark(GContext *ctx, int x, int y) {
  #ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, GColorGreen);
  #else
  graphics_context_set_stroke_color(ctx, GColorWhite);
  #endif
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(x, y+4), GPoint(x+3, y+7));
  graphics_draw_line(ctx, GPoint(x+3, y+7), GPoint(x+8, y));
  graphics_context_set_stroke_width(ctx, 1);
}

// ============================================================================
// DRAW: MAIN MENU (black background)
// ============================================================================
static void draw_main_menu(GContext *ctx, int w, int h) {
  // Black background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  int pad = PBL_IF_ROUND_ELSE(18, 4);
  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  int ty = PBL_IF_ROUND_ELSE(pad + 6, pad);
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorFromHEX(0xFFAA00));
  #else
  graphics_context_set_text_color(ctx, GColorWhite);
  #endif
  graphics_draw_text(ctx, "BASE CAMP", f_lg,
    GRect(0, ty, w, 34),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Decorative icons flanking title
  if(s_icon_font_20) {
    int icon_inset = PBL_IF_ROUND_ELSE(pad + 30, pad + 10);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorFromHEX(0x00FF00));
    #endif
    graphics_draw_text(ctx, FA_TREE, s_icon_font_20,
      GRect(icon_inset, ty + 6, 24, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorFromHEX(0xFFAA00));
    #endif
    graphics_draw_text(ctx, FA_CARET_UP, s_icon_font_20,
      GRect(w - icon_inset - 24, ty + 6, 24, 24),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }

  // Date
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char dbuf[20];
  strftime(dbuf, sizeof(dbuf), "%b %d, %Y", t);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, dbuf, f_sm,
    GRect(0, ty + 30, w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  // Menu items: Tents, Binairo, ---, Yesterday, Stats
  const char *items[] = {"Tents & Trees", "Twilight", "Smoke Signal", "Yesterday", "Stats"};
  int my = ty + 54;
  int row_h = 26;
  int mx = PBL_IF_ROUND_ELSE(pad + 24, pad + 16);

  for(int i = 0; i < 5; i++) {
    bool sel = (s_menu_cursor == i);
    // Separator before Yesterday
    if(i == 3) {
      #ifdef PBL_COLOR
      graphics_context_set_stroke_color(ctx, GColorFromHEX(0x555555));
      #else
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      #endif
      graphics_draw_line(ctx, GPoint(mx, my - 2), GPoint(w - mx, my - 2));
      my += 4;
    }
    if(sel) {
      #ifdef PBL_COLOR
      graphics_context_set_fill_color(ctx, GColorFromHEX(0x005500));
      #else
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      #endif
      graphics_fill_rect(ctx, GRect(mx - 4, my, w - (mx - 4) * 2, row_h - 2), 4, GCornersAll);
    }
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, items[i], sel ? f_md : f_sm,
      GRect(mx + 4, my + 2, w - mx * 2, row_h - 2),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    if(i < NUM_GAMES && game_done_any_today(i))
      draw_checkmark(ctx, w - mx - 8, my + 6);

    my += row_h;
  }

  // Streak
  if(s_streak > 0) {
    char sbuf[16];
    snprintf(sbuf, sizeof(sbuf), "Streak: %d", s_streak);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorOrange);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, sbuf, f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(32, 20), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// DRAW: DIFFICULTY MENU
// ============================================================================
static void draw_diff_menu(GContext *ctx, int w, int h) {
  // Background matches the game theme
  #ifdef PBL_COLOR
  if(s_yesterday)
    graphics_context_set_fill_color(ctx, GColorBlack);
  else if(s_game == GAME_TENTS)
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x005500));
  else if(s_game == GAME_BINAIRO)
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x000055));
  else
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x550000));
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);
  int ty = PBL_IF_ROUND_ELSE(pad + 16, pad + 8);
  int mx = PBL_IF_ROUND_ELSE(50, 30);

  const char *opts[3];
  int num_opts;

  if(s_yesterday) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "YESTERDAY", f_lg,
      GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    opts[0] = "Tents & Trees"; opts[1] = "Twilight"; opts[2] = "Smoke Signal";
    num_opts = 3;
  } else {
    graphics_context_set_text_color(ctx, GColorWhite);
    const char *title = (s_game == GAME_TENTS) ? "TENTS" :
                         (s_game == GAME_BINAIRO) ? "TWILIGHT" : "SMOKE SIGNAL";
    graphics_draw_text(ctx, title, f_lg,
      GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    opts[0] = "Easy"; opts[1] = "Hard"; opts[2] = "Tutorial";
    num_opts = 3;
  }

  int cy = h / 2 - (num_opts * 16);
  int row_h = 32;
  for(int i = 0; i < num_opts; i++) {
    bool sel = (s_diff_cursor == i);
    if(sel) {
      #ifdef PBL_COLOR
      GColor sel_bg = s_yesterday ? GColorFromHEX(0x555555) :
        (s_game == GAME_TENTS) ? GColorFromHEX(0x00AA00) :
        (s_game == GAME_BINAIRO) ? GColorFromHEX(0x0000AA) :
        GColorFromHEX(0xAA0000);
      graphics_context_set_fill_color(ctx, sel_bg);
      #else
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      #endif
      graphics_fill_rect(ctx, GRect(mx, cy, w - mx * 2, row_h - 4), 6, GCornersAll);
    }
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, sel ? GColorWhite : GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, opts[i], sel ? f_md : f_sm,
      GRect(mx + 8, cy + 4, w - mx * 2 - 16, row_h - 4),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    if(!s_yesterday && i < 2 && game_done_today(s_game, i))
      draw_checkmark(ctx, w - mx - 4, cy + 8);
    cy += row_h;
  }
}

// ============================================================================
// DRAW: TUTORIAL
// ============================================================================
static void draw_tutorial(GContext *ctx, int w, int h) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, (s_game == GAME_TENTS) ?
    GColorFromHEX(0x005500) : (s_game == GAME_BINAIRO) ?
    GColorFromHEX(0x000055) : GColorFromHEX(0x550000));
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);
  int ty = PBL_IF_ROUND_ELSE(pad + 8, pad + 2);
  int mx = PBL_IF_ROUND_ELSE(pad + 12, pad + 8);

  const char *title;
  const char *lines[8];
  int nlines;

  // Flag for custom first line with inline icons (Tents page 0)
  bool tn_custom_line = false;

  if(s_game == GAME_TENTS) {
    title = "TENTS & TREES";
    if(s_tut_page == 0) {
      tn_custom_line = true;   // "Every [tree] needs a [tent]" drawn with icons
      lines[0] = "Tents go next to trees";
      lines[1] = "Diagonal doesn't count!";
      lines[2] = "No 2 tents may touch";
      lines[3] = "Match row/col counts";
      nlines = 4;
    } else {
      lines[0] = "Arrows: move cursor";
      lines[1] = "TAP: tent/grass/empty";
      lines[2] = "(or hold SEL to toggle)";
      lines[3] = "Grass = 'no tent here'";
      lines[4] = "Hold DOWN: save & exit";
      nlines = 5;
    }
  } else if(s_game == GAME_BINAIRO) {
    title = "TWILIGHT";
    if(s_tut_page == 0) {
      lines[0] = "Fill grid with suns/moons";
      lines[1] = "No 3 same in a row/col";
      lines[2] = "Equal count per row/col";
      lines[3] = "All rows & cols unique";
      nlines = 4;
    } else {
      lines[0] = "TAP: toggle cell";
      lines[1] = "(or hold SEL to toggle)";
      lines[2] = "Arrows: move cursor";
      lines[3] = "Hold DOWN: save & exit";
      nlines = 4;
    }
  } else {
    title = "SMOKE SIGNAL";
    if(s_tut_page == 0) {
      lines[0] = "Fill campfires with smoke";
      lines[1] = "Smoke rises from the fire";
      lines[2] = "No gaps allowed!";
      lines[3] = "Match row/col counts";
      nlines = 4;
    } else {
      lines[0] = "TAP: fill/empty cell";
      lines[1] = "(or hold SEL to toggle)";
      lines[2] = "Arrows: move cursor";
      lines[3] = "Hold DOWN: save & exit";
      nlines = 4;
    }
  }

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, title, f_lg,
    GRect(0, ty, w, 34),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int ly = ty + 44;
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorYellow);
  #endif

  // Custom first line: "Every [tree] needs a [tent]" with inline icons
  if(tn_custom_line && s_icon_font_20) {
    // Layout: "Every" tree_icon "needs a" tent_icon — centered
    int total_w = 158;
    int lx = (w - total_w) / 2;
    graphics_draw_text(ctx, "Every", f_md,
      GRect(lx, ly, 50, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorFromHEX(0x00FF00));
    #endif
    graphics_draw_text(ctx, FA_TREE, s_icon_font_20,
      GRect(lx + 52, ly, 20, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #endif
    graphics_draw_text(ctx, "needs a", f_md,
      GRect(lx + 74, ly, 64, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorFromHEX(0xFFAA00));
    #endif
    graphics_draw_text(ctx, FA_CARET_UP, s_icon_font_20,
      GRect(lx + 138, ly, 20, 24), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #endif
    ly += 24;
  } else if(tn_custom_line) {
    // Fallback without icon font
    graphics_draw_text(ctx, "Every tree needs a tent", f_md,
      GRect(mx, ly, w - mx * 2, 22), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    ly += 22;
  }

  for(int i = 0; i < nlines; i++) {
    graphics_draw_text(ctx, lines[i], f_md,
      GRect(mx, ly, w - mx * 2, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    ly += 22;
  }

  // Page indicator
  char pbuf[4];
  snprintf(pbuf, sizeof(pbuf), "%d/2", s_tut_page + 1);
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, pbuf, f_sm,
    GRect(0, ly + 2, w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  graphics_context_set_text_color(ctx, GColorWhite);
  const char *hint = (s_tut_page == 0) ? "SEL: next" : "SEL: play";
  graphics_draw_text(ctx, hint, f_sm,
    GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: STATS
// ============================================================================
static void draw_stats(GContext *ctx, int w, int h) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);
  int ty = PBL_IF_ROUND_ELSE(pad + 8, pad + 2);
  int mx = PBL_IF_ROUND_ELSE(pad + 20, pad + 8);

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "STATS", f_lg, GRect(0, ty, w, 34),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  int ly = ty + 40;
  char buf[32];
  #ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorOrange);
  #endif
  snprintf(buf, sizeof(buf), "Streak: %d days", s_streak);
  graphics_draw_text(ctx, buf, f_md, GRect(mx, ly, w - mx * 2, 22),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ly += 26;

  graphics_context_set_text_color(ctx, GColorWhite);
  snprintf(buf, sizeof(buf), "Best: %d days", s_best_streak);
  graphics_draw_text(ctx, buf, f_sm, GRect(mx, ly, w - mx * 2, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ly += 20;
  snprintf(buf, sizeof(buf), "Days played: %d", s_total_days);
  graphics_draw_text(ctx, buf, f_sm, GRect(mx, ly, w - mx * 2, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  ly += 26;

  const char *gn[] = {"Tnt", "Twi", "Smk"};
  for(int g = 0; g < NUM_GAMES; g++) {
    int base = game_base(g);
    int we = persist_exists(base + PG_WINS_EASY) ? persist_read_int(base + PG_WINS_EASY) : 0;
    int wh = persist_exists(base + PG_WINS_HARD) ? persist_read_int(base + PG_WINS_HARD) : 0;
    snprintf(buf, sizeof(buf), "%s: %dE / %dH", gn[g], we, wh);
    graphics_draw_text(ctx, buf, f_sm, GRect(mx, ly, w - mx * 2, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    ly += 18;
  }

  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, "BACK to menu", f_sm,
    GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ============================================================================
// DRAW: TENTS & TREES (green background)
// ============================================================================
#ifdef PBL_COLOR
#define TN_BG        GColorFromHEX(0x005500)  // dark green
#define TN_GRID_BG   GColorFromHEX(0x005500)  // empty cell = bg
#define TN_TREE_CLR  GColorFromHEX(0x00FF00)  // bright green tree
#define TN_TREE_BG   GColorFromHEX(0x005500)  // tree cell bg
#define TN_TENT_CLR  GColorFromHEX(0xFFAA00)  // orange tent
#define TN_TENT_BG   GColorFromHEX(0x555500)  // olive tent bg
#define TN_GRASS_CLR GColorFromHEX(0x55AA55)  // medium green grass
#define TN_GRASS_BG  GColorFromHEX(0x005500)  // grass cell bg
#define TN_CLUE_OK   GColorFromHEX(0x55FF55)  // bright clue met
#endif

static void draw_tn_cell(GContext *ctx, int x, int y, int sz, int r, int c) {
  int val = tn_board[r][c];
  bool locked = g_locked[r][c];
  bool is_cursor = (r == g_cursor_r && c == g_cursor_c);
  bool in_err = tn_error[r][c];
  bool in_warn = tn_warn[r][c];

  #ifdef PBL_COLOR
  if(in_err) graphics_context_set_fill_color(ctx, GColorFromHEX(0x550000));
  else if(in_warn && val == TN_TENT) graphics_context_set_fill_color(ctx, GColorFromHEX(0x555500));
  else if(val == TN_TREE) graphics_context_set_fill_color(ctx, TN_TREE_BG);
  else if(val == TN_TENT) graphics_context_set_fill_color(ctx, TN_TENT_BG);
  else if(val == TN_GRASS) graphics_context_set_fill_color(ctx, TN_GRASS_BG);
  else graphics_context_set_fill_color(ctx, TN_GRID_BG);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(x, y, sz, sz), 3, GCornersAll);

  if(is_cursor) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 3);
    graphics_context_set_stroke_width(ctx, 1);
  } else if(!locked && val == TN_EMPTY) {
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorFromHEX(0x00AA00));
    #else
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #endif
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 3);
  }

  GFont icon_font = s_icon_font_14;
  if(val == TN_TREE) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, TN_TREE_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    if(icon_font)
      graphics_draw_text(ctx, FA_TREE, icon_font, GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    else
      graphics_draw_text(ctx, "T", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else if(val == TN_TENT) {
    #ifdef PBL_COLOR
    GColor tc = in_err ? GColorRed : (in_warn ? GColorFromHEX(0xFFFF00) : TN_TENT_CLR);
    graphics_context_set_text_color(ctx, tc);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    if(icon_font)
      graphics_draw_text(ctx, FA_CARET_UP, icon_font, GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    else
      graphics_draw_text(ctx, "^", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else if(val == TN_GRASS) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, TN_GRASS_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    if(icon_font)
      graphics_draw_text(ctx, FA_CIRCLE, icon_font, GRect(x, y - 1, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    else
      graphics_draw_text(ctx, ".", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_tn_clues(GContext *ctx, int ox, int oy, int cell_sz, int gap, int w) {
  int size = tn_size;
  int cs = 14;
  GFont f_clue = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  char buf[4];

  for(int c = 0; c < size; c++) {
    int cx = ox + c * (cell_sz + gap);
    int cnt = 0;
    for(int r = 0; r < size; r++) if(tn_board[r][c] == TN_TENT) cnt++;
    #ifdef PBL_COLOR
    if(cnt == tn_col_clues[c]) graphics_context_set_text_color(ctx, TN_CLUE_OK);
    else if(cnt > tn_col_clues[c]) graphics_context_set_text_color(ctx, GColorRed);
    else graphics_context_set_text_color(ctx, GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    snprintf(buf, sizeof(buf), "%d", tn_col_clues[c]);
    graphics_draw_text(ctx, buf, f_clue, GRect(cx, oy - cs, cell_sz, cs),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  for(int r = 0; r < size; r++) {
    int ry = oy + r * (cell_sz + gap);
    int cnt = 0;
    for(int c = 0; c < size; c++) if(tn_board[r][c] == TN_TENT) cnt++;
    #ifdef PBL_COLOR
    if(cnt == tn_row_clues[r]) graphics_context_set_text_color(ctx, TN_CLUE_OK);
    else if(cnt > tn_row_clues[r]) graphics_context_set_text_color(ctx, GColorRed);
    else graphics_context_set_text_color(ctx, GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    snprintf(buf, sizeof(buf), "%d", tn_row_clues[r]);
    graphics_draw_text(ctx, buf, f_clue, GRect(ox - cs, ry, cs, cell_sz),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_tents(GContext *ctx, int w, int h) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, TN_BG);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);

  if(s_state == ST_TN_PLAY || s_state == ST_TN_CHECK) {
    int ty = PBL_IF_ROUND_ELSE(pad + 2, 0);
    graphics_context_set_text_color(ctx, GColorWhite);
    const char *dl = (tn_size == 6) ? "TENTS EASY" : "TENTS HARD";
    graphics_draw_text(ctx, dl, f_sm, GRect(0, ty, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cs = 14;
    int gap = 2;
    int cell_sz;
    if(PBL_IF_ROUND_ELSE(true, false)) {
      int safe = (int)(w * 60) / 100 - cs;
      cell_sz = (safe - (tn_size - 1) * gap) / tn_size;
    } else {
      cell_sz = (w - 12 - cs - (tn_size - 1) * gap) / tn_size;
      if(cell_sz > 28) cell_sz = 28;
    }
    if(cell_sz < 12) cell_sz = 12;

    int gw = tn_size * (cell_sz + gap) - gap;
    int gh = tn_size * (cell_sz + gap) - gap;
    int total_w = gw + cs;
    int ox = (w - total_w) / 2 + cs;
    int title_h = PBL_IF_ROUND_ELSE(40, 24);
    int bottom_r = PBL_IF_ROUND_ELSE(22, 16);
    int total_h = gh + cs;
    int avail = h - title_h - bottom_r;
    int oy = title_h + cs;
    if(total_h < avail)
      oy += PBL_IF_ROUND_ELSE((avail - total_h) * 2 / 5, (avail - total_h) / 2);

    draw_tn_clues(ctx, ox, oy, cell_sz, gap, w);
    for(int r = 0; r < tn_size; r++)
      for(int c = 0; c < tn_size; c++)
        draw_tn_cell(ctx, ox + c * (cell_sz + gap), oy + r * (cell_sz + gap), cell_sz, r, c);

    if(s_state == ST_TN_CHECK && tn_error_msg[0]) {
      graphics_context_set_text_color(ctx, GColorRed);
      graphics_draw_text(ctx, tn_error_msg, f_sm,
        GRect(0, h - PBL_IF_ROUND_ELSE(48, 32), w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "TAP:toggle  Hold DN:exit", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(32, 16), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else if(s_state == ST_TN_WIN) {
    int ty = PBL_IF_ROUND_ELSE(pad + 16, 16);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, TN_TENT_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "SOLVED!", f_lg, GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Small completed grid
    int small_sz = 16, small_gap = 1, cs = 14;
    int gw = tn_size * (small_sz + small_gap) - small_gap;
    int sox = (w - gw - cs) / 2 + cs;
    int soy = ty + 50;
    draw_tn_clues(ctx, sox, soy, small_sz, small_gap, w);
    for(int r = 0; r < tn_size; r++)
      for(int c = 0; c < tn_size; c++)
        draw_tn_cell(ctx, sox + c * (small_sz + small_gap), soy + r * (small_sz + small_gap), small_sz, r, c);

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "BACK to menu", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// DRAW: TWILIGHT (blue background, sun & moon)
// ============================================================================
#ifdef PBL_COLOR
#define BN_BG        GColorFromHEX(0x000055)  // dark blue
#define BN_GRID_BG   GColorFromHEX(0x000055)  // empty cell = bg
#define BN_CLUE_BG   GColorFromHEX(0x000055)  // clue cell bg
#define BN_PLAYER_BG GColorFromHEX(0x005555)  // teal player cell
#define BN_SUN_CLR   GColorYellow              // bright sun
#define BN_SUN_CLUE  GColorFromHEX(0xAAAA00)  // dim sun (clue)
#define BN_MOON_CLR  GColorCyan                // bright moon
#define BN_MOON_CLUE GColorFromHEX(0x55AAAA)  // dim moon (clue)
#endif

static void draw_bn_cell(GContext *ctx, int x, int y, int sz, int r, int c) {
  int val = bn_board[r][c];
  bool locked = g_locked[r][c];
  bool is_cursor = (r == g_cursor_r && c == g_cursor_c);
  bool in_err = bn_error[r][c];
  bool in_warn = bn_warn[r][c];

  #ifdef PBL_COLOR
  if(in_err) graphics_context_set_fill_color(ctx, GColorFromHEX(0x550000));
  else if(in_warn && !locked) graphics_context_set_fill_color(ctx, GColorFromHEX(0x555500));
  else if(locked) graphics_context_set_fill_color(ctx, BN_CLUE_BG);
  else if(val != BN_EMPTY) graphics_context_set_fill_color(ctx, BN_PLAYER_BG);
  else graphics_context_set_fill_color(ctx, BN_GRID_BG);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(x, y, sz, sz), 4, GCornersAll);

  if(is_cursor) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 4);
    graphics_context_set_stroke_width(ctx, 1);
  } else if(!locked) {
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorFromHEX(0x5555AA));
    #else
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #endif
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 4);
  }

  GFont icon_font = s_icon_font_14;
  if(val == BN_SUN) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, locked ? BN_SUN_CLUE : BN_SUN_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    if(icon_font)
      graphics_draw_text(ctx, FA_SUN, icon_font, GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    else
      graphics_draw_text(ctx, "S", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  } else if(val == BN_MOON) {
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, locked ? BN_MOON_CLUE : BN_MOON_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    if(icon_font)
      graphics_draw_text(ctx, FA_MOON, icon_font, GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    else
      graphics_draw_text(ctx, "M", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_binairo(GContext *ctx, int w, int h) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, BN_BG);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);

  if(s_state == ST_BN_PLAY || s_state == ST_BN_CHECK) {
    int ty = PBL_IF_ROUND_ELSE(pad + 4, 0);
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "TWILIGHT", f_md, GRect(0, ty, w, 22),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int gap = 2;
    int cell_sz;
    if(PBL_IF_ROUND_ELSE(true, false)) {
      int safe = (int)(w * 62) / 100;
      cell_sz = (safe - (bn_size - 1) * gap) / bn_size;
    } else {
      cell_sz = (w - 16 - (bn_size - 1) * gap) / bn_size;
      if(cell_sz > 32) cell_sz = 32;
    }

    int gw = bn_size * (cell_sz + gap) - gap;
    int gh = bn_size * (cell_sz + gap) - gap;
    int ox = (w - gw) / 2;
    int top_start = ty + 24;
    int bottom_r = PBL_IF_ROUND_ELSE(24, 20);
    int avail_h = h - top_start - bottom_r;
    int oy = top_start;
    if(gh < avail_h)
      oy += PBL_IF_ROUND_ELSE((avail_h - gh) * 2 / 5, (avail_h - gh) / 2);

    for(int r = 0; r < bn_size; r++)
      for(int c = 0; c < bn_size; c++)
        draw_bn_cell(ctx, ox + c * (cell_sz + gap), oy + r * (cell_sz + gap), cell_sz, r, c);

    if(s_state == ST_BN_CHECK && bn_error_msg[0]) {
      graphics_context_set_text_color(ctx, GColorRed);
      graphics_draw_text(ctx, bn_error_msg, f_sm,
        GRect(0, h - PBL_IF_ROUND_ELSE(48, 32), w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }

    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "TAP:toggle  Hold DN:exit", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(32, 16), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  } else if(s_state == ST_BN_WIN) {
    int ty = PBL_IF_ROUND_ELSE(pad + 20, 20);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorYellow);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "SOLVED!", f_lg, GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cell_sz = 20, gap = 2;
    int gw = bn_size * (cell_sz + gap) - gap;
    int ox = (w - gw) / 2;
    int oy = ty + 44;
    for(int r = 0; r < bn_size; r++)
      for(int c = 0; c < bn_size; c++)
        draw_bn_cell(ctx, ox + c * (cell_sz + gap), oy + r * (cell_sz + gap), cell_sz, r, c);

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "BACK to menu", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// DRAW: SMOKE SIGNAL (dark red background, fire & smoke)
// ============================================================================
#ifdef PBL_COLOR
#define SM_BG         GColorFromHEX(0x550000)  // dark red
#define SM_GRID_BG    GColorFromHEX(0x550000)  // empty thermo cell
#define SM_FIRE_CLR   GColorFromHEX(0xFFAA00)  // orange fire (base)
#define SM_SMOKE_CLR  GColorFromHEX(0xAAAAAA)  // light gray smoke
#define SM_FILLED_BG  GColorFromHEX(0xAA5500)  // warm filled bg
#define SM_EMPTY_BG   GColorFromHEX(0x550000)  // unfilled thermo
#define SM_LOCKED_BG  GColorBlack              // non-thermo cell
#define SM_CLUE_OK    GColorFromHEX(0x55FF55)  // clue met
#endif

static void draw_sm_cell(GContext *ctx, int x, int y, int sz, int r, int c) {
  int val = sm_board[r][c];
  int tid = sm_thermo_id[r][c];
  bool is_cursor = (r == g_cursor_r && c == g_cursor_c);
  bool in_err = sm_error[r][c];
  bool in_warn = sm_warn[r][c];

  #ifdef PBL_COLOR
  if(tid < 0) {
    graphics_context_set_fill_color(ctx, SM_LOCKED_BG);
  } else if(in_err) {
    graphics_context_set_fill_color(ctx, GColorFromHEX(0xFF0000));
  } else if(in_warn) {
    graphics_context_set_fill_color(ctx, GColorFromHEX(0x555500));
  } else if(val == SM_FILLED) {
    graphics_context_set_fill_color(ctx, SM_FILLED_BG);
  } else {
    graphics_context_set_fill_color(ctx, SM_EMPTY_BG);
  }
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(x, y, sz, sz), 3, GCornersAll);

  if(is_cursor && tid >= 0) {
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 3);
    graphics_context_set_stroke_width(ctx, 1);
  } else if(tid >= 0 && val == SM_EMPTY) {
    #ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, GColorFromHEX(0xAA5500));
    #else
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    #endif
    graphics_draw_round_rect(ctx, GRect(x, y, sz, sz), 3);
  }

  // Draw icon: fire for base cell, cloud for other filled cells
  if(tid >= 0 && val == SM_FILLED) {
    GFont icon_font = s_icon_font_14;
    bool is_base = (sm_thermo_pos[r][c] == 0);
    if(is_base) {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, SM_FIRE_CLR);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      if(icon_font)
        graphics_draw_text(ctx, FA_FIRE, icon_font, GRect(x, y, sz, sz),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      else
        graphics_draw_text(ctx, "F", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    } else {
      #ifdef PBL_COLOR
      graphics_context_set_text_color(ctx, SM_SMOKE_CLR);
      #else
      graphics_context_set_text_color(ctx, GColorWhite);
      #endif
      if(icon_font)
        graphics_draw_text(ctx, FA_CLOUD, icon_font, GRect(x, y, sz, sz),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
      else
        graphics_draw_text(ctx, "S", fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(x, y, sz, sz),
          GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
  } else if(tid >= 0 && val == SM_EMPTY && sm_thermo_pos[r][c] == 0) {
    // Show dim fire icon on empty base cell so you know where thermos start
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, GColorFromHEX(0xAA5500));
    #else
    graphics_context_set_text_color(ctx, GColorDarkGray);
    #endif
    if(s_icon_font_14)
      graphics_draw_text(ctx, FA_FIRE, s_icon_font_14, GRect(x, y, sz, sz),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_sm_clues(GContext *ctx, int ox, int oy, int cell_sz, int gap, int w) {
  int size = sm_size;
  int cs = 14;
  GFont f_clue = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  char buf[4];
  for(int c = 0; c < size; c++) {
    int cx = ox + c * (cell_sz + gap);
    int cnt = 0;
    for(int r = 0; r < size; r++) if(sm_board[r][c] == SM_FILLED) cnt++;
    #ifdef PBL_COLOR
    if(cnt == sm_col_clues[c]) graphics_context_set_text_color(ctx, SM_CLUE_OK);
    else if(cnt > sm_col_clues[c]) graphics_context_set_text_color(ctx, GColorRed);
    else graphics_context_set_text_color(ctx, GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    snprintf(buf, sizeof(buf), "%d", sm_col_clues[c]);
    graphics_draw_text(ctx, buf, f_clue, GRect(cx, oy - cs, cell_sz, cs),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
  for(int r = 0; r < size; r++) {
    int ry = oy + r * (cell_sz + gap);
    int cnt = 0;
    for(int c = 0; c < size; c++) if(sm_board[r][c] == SM_FILLED) cnt++;
    #ifdef PBL_COLOR
    if(cnt == sm_row_clues[r]) graphics_context_set_text_color(ctx, SM_CLUE_OK);
    else if(cnt > sm_row_clues[r]) graphics_context_set_text_color(ctx, GColorRed);
    else graphics_context_set_text_color(ctx, GColorLightGray);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    snprintf(buf, sizeof(buf), "%d", sm_row_clues[r]);
    graphics_draw_text(ctx, buf, f_clue, GRect(ox - cs, ry, cs, cell_sz),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void draw_smoke(GContext *ctx, int w, int h) {
  #ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, SM_BG);
  #else
  graphics_context_set_fill_color(ctx, GColorBlack);
  #endif
  graphics_fill_rect(ctx, GRect(0, 0, w, h), 0, GCornerNone);

  GFont f_lg = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont f_md = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont f_sm = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int pad = PBL_IF_ROUND_ELSE(18, 4);

  if(s_state == ST_SM_PLAY || s_state == ST_SM_CHECK) {
    int ty = PBL_IF_ROUND_ELSE(pad + 2, 0);
    graphics_context_set_text_color(ctx, GColorWhite);
    const char *dl = (sm_size == 6) ? "SMOKE EASY" : "SMOKE HARD";
    graphics_draw_text(ctx, dl, f_sm, GRect(0, ty, w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int cs = 14, gap = 2, cell_sz;
    if(PBL_IF_ROUND_ELSE(true, false)) {
      int safe = (int)(w * 60) / 100 - cs;
      cell_sz = (safe - (sm_size - 1) * gap) / sm_size;
    } else {
      cell_sz = (w - 12 - cs - (sm_size - 1) * gap) / sm_size;
      if(cell_sz > 28) cell_sz = 28;
    }
    if(cell_sz < 12) cell_sz = 12;
    int gw = sm_size * (cell_sz + gap) - gap;
    int gh = sm_size * (cell_sz + gap) - gap;
    int total_w = gw + cs;
    int ox = (w - total_w) / 2 + cs;
    int title_h = PBL_IF_ROUND_ELSE(40, 24);
    int bottom_r = PBL_IF_ROUND_ELSE(32, 16);
    int total_h = gh + cs;
    int avail = h - title_h - bottom_r;
    int oy = title_h + cs;
    if(total_h < avail)
      oy += PBL_IF_ROUND_ELSE((avail - total_h) * 2 / 5, (avail - total_h) / 2);

    draw_sm_clues(ctx, ox, oy, cell_sz, gap, w);
    for(int r = 0; r < sm_size; r++)
      for(int c = 0; c < sm_size; c++)
        draw_sm_cell(ctx, ox + c * (cell_sz + gap), oy + r * (cell_sz + gap), cell_sz, r, c);

    if(s_state == ST_SM_CHECK && sm_error_msg[0]) {
      graphics_context_set_text_color(ctx, GColorRed);
      graphics_draw_text(ctx, sm_error_msg, f_sm,
        GRect(0, h - PBL_IF_ROUND_ELSE(48, 32), w, 16),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
    }
    graphics_context_set_text_color(ctx, GColorLightGray);
    graphics_draw_text(ctx, "TAP:toggle  Hold DN:exit", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(32, 16), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  } else if(s_state == ST_SM_WIN) {
    int ty = PBL_IF_ROUND_ELSE(pad + 16, 16);
    #ifdef PBL_COLOR
    graphics_context_set_text_color(ctx, SM_FIRE_CLR);
    #else
    graphics_context_set_text_color(ctx, GColorWhite);
    #endif
    graphics_draw_text(ctx, "SOLVED!", f_lg, GRect(0, ty, w, 34),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    int small_sz = 16, small_gap = 1, cs = 14;
    int gw = sm_size * (small_sz + small_gap) - small_gap;
    int sox = (w - gw - cs) / 2 + cs;
    int soy = ty + 50;
    draw_sm_clues(ctx, sox, soy, small_sz, small_gap, w);
    for(int r = 0; r < sm_size; r++)
      for(int c = 0; c < sm_size; c++)
        draw_sm_cell(ctx, sox + c * (small_sz + small_gap), soy + r * (small_sz + small_gap), small_sz, r, c);

    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, "BACK to menu", f_sm,
      GRect(0, h - PBL_IF_ROUND_ELSE(28, 18), w, 16),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

// ============================================================================
// CANVAS PROC
// ============================================================================
static void canvas_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int w = bounds.size.w, h = bounds.size.h;

  switch(s_state) {
    case ST_MAIN_MENU: draw_main_menu(ctx, w, h); break;
    case ST_DIFF_MENU: draw_diff_menu(ctx, w, h); break;
    case ST_TUTORIAL:  draw_tutorial(ctx, w, h); break;
    case ST_STATS:     draw_stats(ctx, w, h); break;
    case ST_TN_PLAY: case ST_TN_CHECK: case ST_TN_WIN:
      draw_tents(ctx, w, h); break;
    case ST_BN_PLAY: case ST_BN_CHECK: case ST_BN_WIN:
      draw_binairo(ctx, w, h); break;
    case ST_SM_PLAY: case ST_SM_CHECK: case ST_SM_WIN:
      draw_smoke(ctx, w, h); break;
  }
}

// ============================================================================
// BUTTON HANDLERS
// ============================================================================
static void launch_game(void) {
  s_diff_cursor = 0;
  s_state = ST_DIFF_MENU;
}

static void start_game(void) {
  if(!(s_tutorial_seen & (1 << s_game))) {
    s_tutorial_seen |= (1 << s_game);
    persist_write_int(P_TUTORIAL_SEEN, s_tutorial_seen);
    s_tut_page = 0;
    s_state = ST_TUTORIAL;
  } else {
    if(s_game == GAME_TENTS) tn_init();
    else if(s_game == GAME_BINAIRO) bn_init();
    else sm_init();
  }
}

static void select_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  switch(s_state) {
    case ST_MAIN_MENU:
      if(s_menu_cursor < NUM_GAMES) {
        s_game = s_menu_cursor; s_yesterday = 0; launch_game();
      } else if(s_menu_cursor == NUM_GAMES) {
        s_yesterday = 1; s_diff_cursor = 0; s_state = ST_DIFF_MENU;
      } else if(s_menu_cursor == NUM_GAMES + 1) {
        s_state = ST_STATS;
      }
      break;
    case ST_DIFF_MENU:
      if(s_yesterday) {
        s_game = s_diff_cursor; s_difficulty = 0; start_game();
      } else {
        if(s_diff_cursor == 2) {
          s_tutorial_seen |= (1 << s_game);
          persist_write_int(P_TUTORIAL_SEEN, s_tutorial_seen);
          s_tut_page = 0; s_state = ST_TUTORIAL;
        } else {
          s_difficulty = s_diff_cursor; start_game();
        }
      }
      break;
    case ST_TUTORIAL:
      if(s_tut_page < 1) { s_tut_page++; }
      else {
        if(s_game == GAME_TENTS) tn_init();
        else if(s_game == GAME_BINAIRO) bn_init();
        else sm_init();
      }
      break;
    case ST_SM_PLAY: case ST_SM_CHECK:
      if(s_state == ST_SM_CHECK) { memset(sm_error, 0, sizeof(sm_error)); sm_error_msg[0] = '\0'; s_state = ST_SM_PLAY; }
      grid_move_right_stay_row();
      break;
    case ST_BN_PLAY: case ST_BN_CHECK:
      if(s_state == ST_BN_CHECK) { memset(bn_error, 0, sizeof(bn_error)); bn_error_msg[0] = '\0'; s_state = ST_BN_PLAY; }
      grid_move_right_stay_row();
      break;
    case ST_TN_PLAY: case ST_TN_CHECK:
      if(s_state == ST_TN_CHECK) { memset(tn_error, 0, sizeof(tn_error)); tn_error_msg[0] = '\0'; s_state = ST_TN_PLAY; }
      grid_move_right_stay_row();
      break;
    default: break;
  }
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  switch(s_state) {
    case ST_MAIN_MENU:
      s_menu_cursor = (s_menu_cursor + 4) % 5;
      break;
    case ST_DIFF_MENU: {
      int n = s_yesterday ? NUM_GAMES : 3;
      s_diff_cursor = (s_diff_cursor + n - 1) % n;
      break;
    }
    case ST_SM_PLAY: case ST_SM_CHECK:
      if(s_state == ST_SM_CHECK) { memset(sm_error, 0, sizeof(sm_error)); sm_error_msg[0] = '\0'; s_state = ST_SM_PLAY; }
      grid_move_up();
      break;
    case ST_BN_PLAY: case ST_BN_CHECK:
      if(s_state == ST_BN_CHECK) { memset(bn_error, 0, sizeof(bn_error)); bn_error_msg[0] = '\0'; s_state = ST_BN_PLAY; }
      grid_move_up();
      break;
    case ST_TN_PLAY: case ST_TN_CHECK:
      if(s_state == ST_TN_CHECK) { memset(tn_error, 0, sizeof(tn_error)); tn_error_msg[0] = '\0'; s_state = ST_TN_PLAY; }
      grid_move_up();
      break;
    default: break;
  }
  layer_mark_dirty(s_canvas);
}

static void down_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  switch(s_state) {
    case ST_MAIN_MENU:
      s_menu_cursor = (s_menu_cursor + 1) % 5;
      break;
    case ST_DIFF_MENU: {
      int n = s_yesterday ? NUM_GAMES : 3;
      s_diff_cursor = (s_diff_cursor + 1) % n;
      break;
    }
    case ST_SM_PLAY: case ST_SM_CHECK:
      if(s_state == ST_SM_CHECK) { memset(sm_error, 0, sizeof(sm_error)); sm_error_msg[0] = '\0'; s_state = ST_SM_PLAY; }
      grid_move_down();
      break;
    case ST_BN_PLAY: case ST_BN_CHECK:
      if(s_state == ST_BN_CHECK) { memset(bn_error, 0, sizeof(bn_error)); bn_error_msg[0] = '\0'; s_state = ST_BN_PLAY; }
      grid_move_down();
      break;
    case ST_TN_PLAY: case ST_TN_CHECK:
      if(s_state == ST_TN_CHECK) { memset(tn_error, 0, sizeof(tn_error)); tn_error_msg[0] = '\0'; s_state = ST_TN_PLAY; }
      grid_move_down();
      break;
    default: break;
  }
  layer_mark_dirty(s_canvas);
}

static void back_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  switch(s_state) {
    case ST_MAIN_MENU:
      window_stack_pop(true);
      break;
    case ST_DIFF_MENU:
      s_yesterday = 0; s_state = ST_MAIN_MENU;
      break;
    case ST_TUTORIAL:
      if(s_tut_page > 0) s_tut_page--;
      else s_state = ST_DIFF_MENU;
      break;
    case ST_STATS:
      s_state = ST_MAIN_MENU;
      break;
    case ST_BN_PLAY: case ST_BN_CHECK:
      if(s_state == ST_BN_CHECK) { memset(bn_error, 0, sizeof(bn_error)); bn_error_msg[0] = '\0'; s_state = ST_BN_PLAY; }
      else grid_move_left();
      break;
    case ST_SM_PLAY: case ST_SM_CHECK:
      if(s_state == ST_SM_CHECK) { memset(sm_error, 0, sizeof(sm_error)); sm_error_msg[0] = '\0'; s_state = ST_SM_PLAY; }
      else grid_move_left();
      break;
    case ST_SM_WIN: case ST_BN_WIN: case ST_TN_WIN:
      s_state = ST_MAIN_MENU;
      break;
    case ST_TN_PLAY: case ST_TN_CHECK:
      if(s_state == ST_TN_CHECK) { memset(tn_error, 0, sizeof(tn_error)); tn_error_msg[0] = '\0'; s_state = ST_TN_PLAY; }
      else grid_move_left();
      break;
    default:
      s_state = ST_MAIN_MENU;
      break;
  }
  layer_mark_dirty(s_canvas);
}

// ============================================================================
// TOUCH INPUT
// ============================================================================
#define TAP_COOLDOWN_MS  400    // minimum ms between detected taps
static uint32_t s_last_tap_time = 0;

static void do_tap_action(void);  // forward decl

// Touch service handler — tap screen anywhere to toggle
static void touch_handler(const TouchEvent *event, void *context) {
  (void)context;
  if(event->type != TouchEvent_Touchdown) return;
  time_t t; uint16_t ms;
  time_ms(&t, &ms);
  uint32_t now = (uint32_t)(t * 1000 + ms);
  if(now - s_last_tap_time < TAP_COOLDOWN_MS) return;
  s_last_tap_time = now;
  do_tap_action();
}

// TAP handler — Pebble accel tap or mapped to middle button
static void do_tap_action(void) {
  switch(s_state) {
    case ST_SM_PLAY:
      sm_toggle_and_check();
      break;
    case ST_SM_CHECK:
      memset(sm_error, 0, sizeof(sm_error)); sm_error_msg[0] = '\0'; s_state = ST_SM_PLAY;
      sm_toggle_and_check();
      break;
    case ST_BN_PLAY:
      bn_toggle_and_check();
      break;
    case ST_BN_CHECK:
      memset(bn_error, 0, sizeof(bn_error)); bn_error_msg[0] = '\0'; s_state = ST_BN_PLAY;
      bn_toggle_and_check();
      break;
    case ST_TN_PLAY:
      tn_toggle_and_check();
      break;
    case ST_TN_CHECK:
      memset(tn_error, 0, sizeof(tn_error)); tn_error_msg[0] = '\0'; s_state = ST_TN_PLAY;
      tn_toggle_and_check();
      break;
    default: return;
  }
  layer_mark_dirty(s_canvas);
}

static void select_long_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  // Long-press SELECT as fallback toggle for devices where accel tap doesn't work
  if(s_state == ST_SM_PLAY || s_state == ST_SM_CHECK ||
     s_state == ST_BN_PLAY || s_state == ST_BN_CHECK ||
     s_state == ST_TN_PLAY || s_state == ST_TN_CHECK) {
    do_tap_action();
  }
}

static void down_long_click(ClickRecognizerRef ref, void *ctx) {
  (void)ref; (void)ctx;
  if(s_state == ST_SM_PLAY || s_state == ST_SM_CHECK) {
    save_game_progress(GAME_SMOKE, sm_board, sm_size);
    s_state = ST_MAIN_MENU;
    layer_mark_dirty(s_canvas);
  } else if(s_state == ST_BN_PLAY || s_state == ST_BN_CHECK) {
    save_game_progress(GAME_BINAIRO, bn_board, bn_size);
    s_state = ST_MAIN_MENU;
    layer_mark_dirty(s_canvas);
  } else if(s_state == ST_TN_PLAY || s_state == ST_TN_CHECK) {
    save_game_progress(GAME_TENTS, tn_board, tn_size);
    s_state = ST_MAIN_MENU;
    layer_mark_dirty(s_canvas);
  }
}

static void click_config(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 400, select_long_click, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN, 500, down_long_click, NULL);
}

// ============================================================================
// WINDOW
// ============================================================================
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, canvas_proc);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

// ============================================================================
// INIT / DEINIT
// ============================================================================
static void init(void) {
  // Load persistence
  if(persist_exists(P_LAST_DAY)) s_last_day = persist_read_int(P_LAST_DAY);
  if(persist_exists(P_STREAK)) s_streak = persist_read_int(P_STREAK);
  if(persist_exists(P_BEST_STREAK)) s_best_streak = persist_read_int(P_BEST_STREAK);
  if(persist_exists(P_TOTAL_DAYS)) s_total_days = persist_read_int(P_TOTAL_DAYS);
  if(persist_exists(P_TUTORIAL_SEEN)) s_tutorial_seen = persist_read_int(P_TUTORIAL_SEEN);

  // Reset streak if more than 1 day gap
  int today = day_number();
  if(s_last_day > 0 && today - s_last_day > 1) {
    s_streak = 0;
    persist_write_int(P_STREAK, 0);
  }

  // Load icon fonts
  s_icon_font_20 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_20));
  s_icon_font_14 = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_ICON_FONT_14));

  // Create window
  s_win = window_create();
  window_set_click_config_provider(s_win, click_config);
  window_set_window_handlers(s_win, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_win, true);

  // Subscribe to touch service for tap input
  touch_service_subscribe(touch_handler, NULL);
}

static void deinit(void) {
  touch_service_unsubscribe();
  if(s_icon_font_20) fonts_unload_custom_font(s_icon_font_20);
  if(s_icon_font_14) fonts_unload_custom_font(s_icon_font_14);
  window_destroy(s_win);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
