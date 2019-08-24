#include <iostream>
#include <fstream>
#include <stack>
#include <vector>
#include <algorithm>
#include <atomic>
#include <limits>
#include <forward_list>
#include <array>
#include <thread>
#include <arm_neon.h>

// g++ -o cc4 -std=c++11 -g -O3 -march=native cc4.cpp && time ./cc4 test.rgb 1920 1080 1 50 black 0 50 0 50 0 50

using namespace std;

typedef struct {
  string name = "none";
  unsigned char lor = 255, hir = 0, log = 255, hig = 0, lob = 255, hib = 0;
} threshold;

typedef struct {
  unsigned char r, g, b;
} rgb;

typedef struct {
  unsigned start, end, color;
} rle;

typedef struct {
  unsigned x0, y0, x1, y1, x0y0, x0y1, x1y0, x1y1;
  unsigned xc, yc, count, color;
  forward_list<rle> top, bottom;
} region;

template<unsigned width, size_t numThresholds>
void foo(rgb *in, unsigned height, unsigned minCount, array<threshold, numThresholds> &thresholds, vector<vector<rle> > &groups) {
  groups.clear();
  groups.reserve(height);
  for (unsigned y=0; y<height; y++) {
    unsigned prev = 0;
    groups.push_back(vector<rle>());
    uint8_t colors[16] = {0};
    for (unsigned x=0; x<width/4; x++) {
      if (!(x&15)) {
        uint8x16x3_t pxs = vld3q_u8(&in[x + y*width].r);
        uint8x16_t color = vdupq_n_u8(0);
        for (unsigned ti = 0; ti < numThresholds; ti++) {
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
      unsigned cur = colors[x&15];
      if (cur && cur != prev)
        groups[y].push_back({x, width/4, cur});
      if (cur != prev && prev)
        groups[y].rbegin()->end = x;
      prev = cur;
    }
  }
}

int main(int argc, char **argv) {
  argv++;
  ifstream infile(*argv++, ios::binary);
  static const unsigned width = 1920; atoi(*argv++);
  unsigned height = atoi(*argv++);
//   unsigned enlarge = atoi(*argv++);
  argv++;
  unsigned minCount = atoi(*argv++);

  array<threshold, 8> thresholds;
  for (auto &a : thresholds) {
    if (!*argv)
      break;
    a.name = *argv++;
    a.lor = atoi(*argv++);
    a.hir = atoi(*argv++);
    a.log = atoi(*argv++);
    a.hig = atoi(*argv++);
    a.lob = atoi(*argv++);
    a.hib = atoi(*argv++);
  }

  rgb *in = new rgb [width * height];
  vector<region> complete, incomplete;
  vector<vector<rle> > groups[4];
  stack<region *> freed;

  while (infile.read(reinterpret_cast<char*>(in), width * height * sizeof(rgb))) {
    size_t a = 0, b = 0, d = 0;
    for (int benchmark=0; benchmark<1000; benchmark++) {
      thread threads[3];
      complete.clear();
      incomplete.clear();
      while (!freed.empty())
        freed.pop();
      complete.reserve(32);
      incomplete.reserve(32);
      for (unsigned ti = 1; ti<4; ti++)
        threads[ti-1] = thread(foo<width, 8>, in+width*ti/4, height, minCount, ref(thresholds), ref(groups[ti]));
      foo<width, 8>(in, height, minCount, thresholds, groups[0]);
      for (auto &ti : threads)
        ti.join();
      for (unsigned y=0; y<height; y++) {
        for (auto &r : incomplete) {
          if (r.bottom.empty() && r.count) {
            if (r.count > minCount)
              complete.push_back(r);
            r.count = 0;
            freed.push(&r);
          }
          r.top = r.bottom;
          r.bottom.clear();
        }
        size_t c = 0;
        vector<rle> combinedGroups;
        bool doFirst = true;
        for (unsigned ti = 1; ti<4; ti++) {
          for (auto const &g : groups[ti-1][y])
            if (doFirst || &g != &(*(groups[ti-1][y].cbegin())))
              combinedGroups.push_back({g.start+(ti-1)*width/4, g.end+(ti-1)*width/4, g.color});
          if (groups[ti][y].size() && groups[ti][y][0].start == 0 && groups[ti-1][y].size() && groups[ti-1][y].rbegin()->end == width/4 && groups[ti][y][0].color == groups[ti-1][y].rbegin()->color) {
            combinedGroups.rbegin()->end = groups[ti][y][0].end+ti*width/4;
            doFirst = false;
          } else {
            doFirst = true;
          }
        }
        for (auto &g : groups[3][y])
          if (doFirst || &g != &(*(groups[3][y].begin())))
            combinedGroups.push_back({g.start+3*width/4, g.end+3*width/4, g.color});
        for (auto &g : combinedGroups) {
          c++;
          forward_list<region*> connected;
          for (auto &r : incomplete)
            if (r.color == g.color && r.count)
              for (auto &rg : r.top)
                if (rg.start < g.end && g.start < rg.end)
                  connected.push_front(&r);
          unsigned start = g.start, end = g.end, endm1 = end - 1;
    //       d += end-start;
          if (connected.empty()) {
            forward_list<rle> g0(1, {g});
            region r = {start, y, endm1, y, start+y, start-y+height, endm1-y+height, endm1+y,
              (end*end-end-start*start+start)>>1, y*(end-start), end-start, g.color};
            r.bottom.push_front(g);
            if (freed.empty()) {
              incomplete.push_back(r);
            } else {
              *freed.top() = r;
              freed.pop();
            }
          } else {
            region &r = *connected.front();
            r.x0 = min(r.x0, start);
            r.y0 = min(r.y0, y);
            r.x1 = max(r.x1, endm1);
            r.y1 = max(r.y1, y);
            r.x0y0 = min(r.x0y0, start+y);
            r.x0y1 = min(r.x0y1, start-y+height);
            r.x1y0 = max(r.x1y0, endm1-y+height);
            r.x1y1 = max(r.x1y1, endm1+y);
            r.count += end-start;
            r.xc += (end*end-end-start*start+start)>>1;
            r.yc += y*(end-start);
            r.bottom.push_front(g);
            connected.pop_front();
            for (auto &r2 : connected) {
              r.x0 = min(r.x0, r2->x0);
              r.y0 = min(r.y0, r2->y0);
              r.x1 = max(r.x1, r2->x1);
              r.y1 = max(r.y1, r2->y1);
              r.x0y0 = min(r.x0y0, r2->x0y0);
              r.x0y1 = min(r.x0y1, r2->x0y1);
              r.x1y0 = max(r.x1y0, r2->x1y0);
              r.x1y1 = max(r.x1y1, r2->x1y1);
              r.count += r2->count;
              r.xc += r2->xc;
              r.yc += r2->yc;
              r2->count = 0;
              r.top.splice_after(r.top.cbegin(), r2->top);
              r.bottom.splice_after(r.bottom.cbegin(), r2->bottom);
              freed.push(r2);
            }
          }
        }
    //     a = max(a, c);
    //     b += c;
      }
      for (auto &r : incomplete)
        if (r.count > minCount)
          complete.push_back(r);
    }
    cout << a << ' ' << b << ' ' << d << ' ';
    a = 0;
    for (auto &i : complete)
      a += i.count;
    cout << a << endl;
    for (auto &i : complete)
      cout << i.count << ' ' << thresholds[i.color-1].name << ' ' << (i.xc/i.count) << ' ' << (i.yc/i.count) << ' ' << i.x0 << ' ' << i.y0 << ' ' << i.x1 << ' ' << i.y1 << ' ' << i.x0y0 << ' ' << i.x0y1 << ' ' << i.x1y0 << ' ' << i.x1y1 << ' ' << endl;
  }

  infile.close();
  return 0;
}
