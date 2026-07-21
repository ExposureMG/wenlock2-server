#include <stdio.h>
#include <libcaptcha.h>

/*
 * Automatically randomize position of glyphs in the array
 * by x and then y axis
 */

int main() {
  char * str = "letsjump";
  char * fontfile = "../ttf/dejavu.ttf";

  lc_fontBuffer *font = lc_create_font(fontfile);
  lc_arrGlyph *arr;
  lc_bmp * bmp;

  if (!font) {
    perror("lc_create_font()");
    return 1;
  }

  arr = lc_str_to_arr(font, str, 38, 0);

  /*
   * Glyph position randomization for both x and y axis
   */
  lc_randomize_arr_x(arr, 30);
  lc_randomize_arr_y(arr, 40);

  if (!arr) {
    perror("lc_str_to_arr()");
    return 1;
  }

  bmp = lc_arr_to_bmp(arr);

  if (!bmp) {
    perror("lc_arr_to_bmp()");
    return 1;
  }

  lc_save_png("./example3.png", bmp);

  lc_free(arr);
  lc_free(bmp);
  lc_free(font);
  return 0;
}
