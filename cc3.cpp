#include <iostream>
#include <fstream>
#include <stack>
#include <vector>
#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <arm_neon.h>

// #define BENCHMARK

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
  if (pixel != enptyList.begin()) {
    if (pixel->count > minCount)
      complete.push_back(*pixel);
    pixel->count = 0;
    freed.push(pixel);
  }
}

void foo(const rgb *in, const unsigned width, const unsigned height,
         const unsigned minCount, const unsigned yStart, const vector<threshold> &thresholds,
         vector<region> &complete, vector<region> &incomplete, stack<vector<region>::iterator> &freed,
         vector<vector<region>::iterator> &topRow, vector<vector<region>::iterator> &bottomRow,
         set<pair<vector<region>::iterator, vector<region>::iterator> > &mergeSet) {
//   complete.clear();
//   incomplete.clear();
  complete.reserve(32);
  incomplete.reserve(32);
  topRow.reserve(width);
  uint16x8_t inc = {65535, 0, 1, 0, 65535, 65535, 1, 1};
  uint16x8_t hinc = {(uint16_t)width, 65535, (uint16_t)-width, 1, (uint16_t)(width-1), (uint16_t)(width+1), (uint16_t)(-1-width), (uint16_t)(1-width)};
  uint16x8_t bounds = {0, (uint16_t)-yStart, 0, (uint16_t)yStart, (uint16_t)-yStart, (uint16_t)(-height-yStart), (uint16_t)(height+yStart), (uint16_t)yStart};
  auto prev = enptyList.begin();
  uint8_t colors[16] = {0};
//   cout << "1st " << incomplete.size() << ' ' << freed.size() << endl;
  for (unsigned x=0; x<width; x++) {
    auto result = enptyList.begin();
    if (!(x&15)) {
      uint8x16x3_t pxs = vld3q_u8(&in[x + yStart*width].r);
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
      if (x && prev->count && prev->color == ti) { //pixel matches to the left
        result = prev;
        result->count++;
        result->xc += x;
        result->yc += yStart;
        vst1q_u16(&result->x0, vmaxq_u16(vld1q_u16(&result->x0), bounds));
        result->recent += 2;
      }
      if (result == enptyList.begin()) {  //made it to the current pixel and no connections have been found
        region r = {0, 0, 0, 0, 0, 0, 0, 0, 0x8002, ti, x, yStart, 1};
//         cout << "tm " << x << ' ' << r.count << ' ' << r.recent << endl;
        vst1q_u16(&r.x0, bounds);
        if (freed.empty()) {
          incomplete.push_back(r);
          result = --incomplete.end();
        } else {
          result = freed.top();
          freed.pop();
          *result = r;
        }
      }
    }
    topRow.push_back(prev);
    prev = result;
    bounds = vaddq_u16(bounds, inc);
  }
  topRow.push_back(prev);
//   cout << "2nd " << incomplete.size() << ' ' << freed.size() << endl;
  bounds = vaddq_u16(bounds, hinc);
  bottomRow = topRow;
  for (unsigned y=yStart+1; y<height; y++) {
    prev = enptyList.begin();
    for (unsigned x=0; x<width; x++) {
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
        if (x && prev->count && prev->color == ti) { //pixel matches to the left
          result = prev;
          result->count++;
          result->xc += x;
          result->yc += y;
          vst1q_u16(&result->x0, vmaxq_u16(vld1q_u16(&result->x0), bounds));
          result->recent++;
        }
        for (unsigned xi=x ? (x-1) : 0; xi<=x+1 && xi<width; xi++) {
          if (bottomRow[xi]->count && bottomRow[xi]->color == ti) { //pixel matches a region above
            if (result->count == 0) {  //pixel has not yet been connected to a region
              result = bottomRow[xi];
              result->count++;
              result->xc += x;
              result->yc += y;
              vst1q_u16(&result->x0, vmaxq_u16(vld1q_u16(&result->x0), bounds));
              result->recent++;
            } else if(result != bottomRow[xi]) {  //combine regions (current pixel has already been added
//               cout << "mm " << x << ' ' << xi << ' ' << y << ' ' << result->count << ' ' << bottomRow[xi]->count << ' ' << result->recent << ' ' << bottomRow[xi]->recent << endl;
              if (result->recent & 0x8000) {
                result->recent--;
                bottomRow[xi]->recent++;
                swap(result, bottomRow[xi]);
              }
              if (result->recent & 0x8000) {
                if (result < bottomRow[xi])
                  swap(result, bottomRow[xi]);
                mergeSet.insert(make_pair(result, bottomRow[xi]));
              } else {
                if (y == 607)
                  cout << x << " e " << result->count << ' ' << result->recent << ' ' << bottomRow[xi]->count << ' ' << bottomRow[xi]->recent << endl;
                bottomRow[xi]->xc += result->xc;
                bottomRow[xi]->yc += result->yc;
                uint16x8_t rb = vld1q_u16(&result->x0);
                uint16x8_t nb = vld1q_u16(&bottomRow[xi]->x0);
                vst1q_u16(&bottomRow[xi]->x0, vmaxq_u16(rb, nb));
                bottomRow[xi]->count += result->count;
                result->count = 0;
                result->recent--;
                bottomRow[xi]->recent++;
                result = bottomRow[xi];
              }
            }
          }
        }
        if (result == enptyList.begin()) {  //made it to the current pixel and no connections have been found
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
        }
      }
      if (x && bottomRow[x-1] != enptyList.begin() && !--bottomRow[x-1]->recent)
        retirePixel(bottomRow[x-1], minCount, complete, freed);
      if (x)
        bottomRow[x-1] = prev;
      prev = result;
      bounds = vaddq_u16(bounds, inc);
    }
    if (bottomRow[width-1] != enptyList.begin() && !--bottomRow[width-1]->recent)
      retirePixel(bottomRow[width-1], minCount, complete, freed);
    bottomRow[width-1] = prev;
    bounds = vaddq_u16(bounds, hinc);
  }
}

int main(int argc, char **argv) {
  argv++;
  ifstream infile(*argv++, ios::binary);
  unsigned width = atoi(*argv++);
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
  vector<vector<region>::iterator> row;
  set<pair<vector<region>::iterator, vector<region>::iterator> > mergeSet;

  rgb *in = new rgb [width * height];
  unsigned a = 0;

  while (infile.read(reinterpret_cast<char*>(in), width * height * sizeof(rgb))) {
#ifdef BENCHMARK
    for (int benchmark=0; benchmark<100; benchmark++) {
      complete.clear();
      incomplete.clear();
#endif
      vector<vector<region>::iterator> rowIgnore, row0, row1;
      foo(in, width, height/2, minCount, 0, thresholds, complete, incomplete, freed, rowIgnore, row0, mergeSet);
      for (unsigned x=0; x<width; x++)
        if (rowIgnore[x] != enptyList.begin() && !(--rowIgnore[x]->recent & 0x7fff))
          retirePixel(rowIgnore[x], minCount, complete, freed);
      foo(in, width, height, minCount, height/2, thresholds, complete, incomplete, freed, row1, row, mergeSet);
//       for (unsigned x=0; x<width; x++)
//         if (row[x] != enptyList.begin())
//           cout << "row " << (row[x]-incomplete.begin()) << ' ' << x << ' ' << row[x]->count << ' ' << row[x]->color << ' ' << row[x]->recent << endl;
//       for (unsigned x=0; x<width; x++)
//         if (row0[x] != enptyList.begin())
//           cout << "row0 " << (row0[x]-incomplete.begin()) << ' ' << x << ' ' << row0[x]->count << ' ' << row0[x]->color << ' ' << row0[x]->recent << endl;
//       for (unsigned x=0; x<width; x++)
//         if (row1[x] != enptyList.begin())
//           cout << "row1 " << (row1[x]-incomplete.begin()) << ' ' << x << ' ' << row1[x]->count << ' ' << row1[x]->color << ' ' << row1[x]->recent << endl;
//       for (auto &i : incomplete)
//         cout << "inc " << i.count << ' ' << i.color << ' ' << i.recent << endl;
//       for (auto &i : complete)
//         cout << "com " << i.count << ' ' << i.color << ' ' << i.recent << endl;
      for (unsigned x=0; x<width; x++) {
        for (unsigned xi=x ? (x-1) : 0; xi<=x+1 && xi<width; xi++) {
//           if (row0[xi]->color == row1[x]->color && row0[xi] != row1[x])
//             cout << x << ' ' << xi << ' ' << row1[x]->count << ' ' << row0[xi]->count << ' ' << row1[x]->color << ' ' << row0[xi]->color << endl;
          if (row0[xi]->count && row0[xi]->color == row1[x]->color && row0[xi] != row1[x]) { //pixel matches a region above
            row1[x]->xc += row0[xi]->xc;
            row1[x]->yc += row0[xi]->yc;
            uint16x8_t rb = vld1q_u16(&row1[x]->x0);
            uint16x8_t nb = vld1q_u16(&row0[xi]->x0);
            vst1q_u16(&row1[x]->x0, vmaxq_u16(rb, nb));
            row1[x]->count += row0[xi]->count;
            row0[xi]->count = 0;
            a++;
          }
        }
        if (x && !--row0[x-1]->recent)
          retirePixel(row0[x-1], minCount, complete, freed);
//         if (row1[x] != enptyList.begin() && !(--row1[x]->recent & 0x7fff))
//           retirePixel(row1[x], minCount, complete, freed);
      }
      retirePixel(row0[width-1], minCount, complete, freed);
      for (auto &i : mergeSet) {
        cout << "ms " << i.first->count << ' ' << i.second->count << endl;
        i.first->xc += i.second->xc;
        i.first->yc += i.second->yc;
        uint16x8_t rb = vld1q_u16(&i.first->x0);
        uint16x8_t nb = vld1q_u16(&i.second->x0);
        vst1q_u16(&i.first->x0, vmaxq_u16(rb, nb));
        i.first->count += i.second->count;
        i.second->count = 0;
        a++;
      }
      for (unsigned x=0; x<width; x++)
        retirePixel(row1[x], minCount, complete, freed);
      for (unsigned x=0; x<width; x++)
        retirePixel(row[x], minCount, complete, freed);
#ifdef BENCHMARK
    }
#endif
  }

  cout << a << ' ' << mergeSet.size() << ' ' << incomplete.capacity() << endl;
  for (auto &i : complete)
    if (i.count)
      cout << i.count << ' ' << thresholds[i.color-1].name << ' ' << (i.xc/i.count) << ' ' << (i.yc/i.count) << ' ' << 65536-i.x0 << ' ' << 65536-i.y0 << ' ' << i.x1 << ' ' << i.y1 << ' ' << 65536-i.x0y0 << ' ' << 65536-i.x0y1 << ' ' << i.x1y0 << ' ' << i.x1y1 << ' ' << endl;

  infile.close();
  return 0;
}
