#include <iostream>
#include <fstream>
#include <stack>
#include <vector>
#include <algorithm>
#include <atomic>
#include <limits>
#include <arm_neon.h>

// with -fprofile-use: 1000/27.657=36.16 fps

using namespace std;

typedef struct {
  string name;
  unsigned int lor, hir, log, hig, lob, hib;
} threshold;

typedef struct {
  unsigned char r, g, b;
} rgb;

typedef struct {
  unsigned short x0, y0, x1, y1, x0y0, x0y1, x1y0, x1y1, recent, color;
  unsigned xc, yc, count;
} region;

vector<region> enptyList(1, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, numeric_limits<unsigned>::max(), 0});

void retirePixel(vector<region>::iterator &pixel, const int minCount,
                 vector<region> &complete, stack<vector<region>::iterator> &freed) {
  if (pixel != enptyList.begin() && pixel->count > minCount)
    complete.push_back(*pixel);
  if (pixel != enptyList.begin()) {
    pixel->count = 0;
    freed.push(pixel);
  }
}

int main(int argc, char **argv) {
  argv++;
  ifstream infile(*argv++, ios::binary);
//   unsigned width = atoi(*argv++);
  static const unsigned width = 1920; argv++;
  unsigned height = atoi(*argv++);
//   unsigned enlarge = atoi(*argv++);
  argv++;
  unsigned minCount = atoi(*argv++);

  vector<threshold> thresholds;
  while (*argv) {
    threshold a;
    a.name = *argv++;
    a.lor = atoi(*argv++);
    a.hir = atoi(*argv++);
    a.log = atoi(*argv++);
    a.hig = atoi(*argv++);
    a.lob = atoi(*argv++);
    a.hib = atoi(*argv++);
    thresholds.push_back(a);
  }

  vector<region> complete, incomplete;
  stack<vector<region>::iterator> freed;
  vector<vector<region>::iterator> row(width, enptyList.begin());
  unsigned a = 0, b = 0, c = 0, d = 0;

  rgb *in = new rgb [width * height];

  while (infile.read(reinterpret_cast<char*>(in), width * height * sizeof(rgb))) {
    for (int benchmark=0; benchmark<1000; benchmark++) {
    complete.clear();
    incomplete.clear();
    complete.reserve(32);
    incomplete.reserve(32);
    uint16x8_t inc = {65535, 0, 1, 0, 65535, 65535, 1, 1};
    uint16x8_t hinc = {(uint16_t)width, 65535, (uint16_t)-width, 1, (uint16_t)(width-1), (uint16_t)(width+1), (uint16_t)(-1-width), (uint16_t)(1-width)};
    uint16x8_t bounds = {0, 0, 0, 0, 0, (uint16_t)-height, (uint16_t)height, 0};
    unsigned short nt = thresholds.size();
    for (unsigned y=0; y<height; y++) {
      auto prev = enptyList.begin();
      for (unsigned x=0; x<width; x++) {
//         rgb px = in[x + y*width];
        auto result = enptyList.begin();
        static uint8_t colors[16] = {0};
        if (!(x&15)) {
          uint8x16x3_t pxs = vld3q_u8(&in[x + y*width].r);
          uint8x16_t color = vdupq_n_u8(0);
          for (unsigned short ti = 0; ti < 8; ti++) { // Hardcoding this limit make a significant improvement to performance
            uint8x16_t cnt = vcgeq_u8(pxs.val[0], vdupq_n_u8(thresholds[ti].lor));
            cnt = vandq_u8(cnt, vcleq_u8(pxs.val[0], vdupq_n_u8(thresholds[ti].hir)));
            cnt = vandq_u8(cnt, vcgeq_u8(pxs.val[1], vdupq_n_u8(thresholds[ti].log)));
            cnt = vandq_u8(cnt, vcleq_u8(pxs.val[1], vdupq_n_u8(thresholds[ti].hig)));
            cnt = vandq_u8(cnt, vcgeq_u8(pxs.val[2], vdupq_n_u8(thresholds[ti].lob)));
            cnt = vandq_u8(cnt, vcleq_u8(pxs.val[2], vdupq_n_u8(thresholds[ti].hib)));
            color = vmaxq_u8(color, vandq_u8(cnt, vdupq_n_u8(ti+1)));
          }
          vst1q_u8(colors, color);
        }
        unsigned short ti = colors[x&15];
        if (ti) {
//         for (unsigned short ti = 0; ti < thresholds.size(); ti++) {
//           const auto &t = thresholds[ti];
//           if (t.lor <= px.r && px.r <= t.hir && t.log <= px.g && px.g <= t.hig &&
//               t.lob <= px.b && px.b <= t.hib) {
            if (x && prev->count && prev->color == ti) { //pixel matches to the left
              result = prev;
              result->count++;
              result->xc += x;
              result->yc += y;
              vst1q_u16(&result->x0, vmaxq_u16(vld1q_u16(&result->x0), bounds));
              result->recent++;
              b++;
            }
            for (unsigned xi=x ? (x-1) : 0; xi<=x+1 && xi<width; xi++) {
              if (row[xi]->count && row[xi]->color == ti) { //pixel matches a region above
                if (result->count == 0) {  //pixel has not yet been connected to a region
                  result = row[xi];
                  result->count++;
                  result->xc += x;
                  result->yc += y;
                  vst1q_u16(&result->x0, vmaxq_u16(vld1q_u16(&result->x0), bounds));
                  result->recent++;
                  a++;
                } else if(result != row[xi]) {  //combine regions (current pixel has already been added
                  result->xc += row[xi]->xc;
                  result->yc += row[xi]->yc;
                  uint16x8_t rb = vld1q_u16(&result->x0);
                  uint16x8_t nb = vld1q_u16(&row[xi]->x0);
                  vst1q_u16(&result->x0, vmaxq_u16(rb, nb));
                  result->count += row[xi]->count;
                  row[xi]->count = 0;
                  c++;
                }
              }
            }
            if (result == enptyList.begin()) {  //made it to the current pixel and no connections have been found
//               cout << "a " << idNext.load() << ' ' << x << ' ' << y << ' ' << t.name << ' ' << rows[1][x]->name << ' ' << rows[1][x]->id << endl;
              region r = {0, 0, 0, 0, 0, 0, 0, 0, 1, ti, x, y, 1};
              vst1q_u16(&r.x0, bounds);
              if (freed.empty()) {
                incomplete.push_back(r);
                result = --incomplete.end();
              } else {
                result = freed.top();
                freed.pop();
                *result = r;
              }
              d++;
            }
//             break;
//           }
        }
        if (x && !--row[x-1]->recent)
          retirePixel(row[x-1], minCount, complete, freed);
        if (x)
          row[x-1] = prev;
        prev = result;
        bounds = vaddq_u16(bounds, inc);
      }
      if (!--row[width-1]->recent)
        retirePixel(row[width-1], minCount, complete, freed);
      row[width-1] = prev;
      bounds = vaddq_u16(bounds, hinc);
    }
    for (unsigned x=0; x<width; x++)
      retirePixel(row[x], minCount, complete, freed);
    }
  }

  cout << d << ' ' << a << ' ' << b << ' ' << c << ' ';
  c = 0;
  for (auto &i : complete)
    c += i.count;
  cout << c << ' ' << incomplete.capacity() << endl;
  for (auto &i : complete)
    if (i.count)
      cout << i.count << ' ' << thresholds[i.color-1].name << ' ' << (i.xc/i.count) << ' ' << (i.yc/i.count) << ' ' << 65536-i.x0 << ' ' << 65536-i.y0 << ' ' << i.x1 << ' ' << i.y1 << ' ' << 65536-i.x0y0 << ' ' << 65536-i.x0y1 << ' ' << i.x1y0 << ' ' << i.x1y1 << ' ' << endl;

  infile.close();
  return 0;
}
