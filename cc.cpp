#include <iostream>
#include <fstream>
#include <list>
#include <forward_list>
#include <deque>
#include <vector>
#include <algorithm>
#include <atomic>

using namespace std;

typedef struct {
  string name;
  unsigned int lor, hir, log, hig, lob, hib;
} threshold;

typedef struct {
  unsigned char r, g, b;
} rgb;

class region;
class region {
public:
  unsigned xc, yc, x0, y0, x1, y1, x0y0, x0y1, x1y0, x1y1, count, id;
  string name;
  list<list<region>::iterator>::iterator it;
};

void popRow(deque<vector<list<region>::iterator> > &rows,
            deque<list<list<region>::iterator> > &recent,
            const int minCount, list<region> &complete, list<region> &incomplete) {
  rows.back().clear();
  rows.pop_back();
  while (!recent.back().empty()) {
    if (recent.back().front()->count > minCount)
      complete.push_front(*recent.back().front());
    incomplete.erase(recent.back().front());
    recent.back().pop_front();
  }
  recent.pop_back();
}

int main(int argc, char **argv) {
  argv++;
  ifstream infile(*argv++, ios::binary);
  unsigned width = atoi(*argv++);
  unsigned height = atoi(*argv++);
  unsigned enlarge = atoi(*argv++);
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

  list<region> complete, incomplete;
  deque<list<list<region>::iterator> > recent;
  deque<vector<list<region>::iterator> > rows;
  atomic<unsigned> idNext(1);
  unsigned a = 0, b = 0, c = 0;

  rgb *in = new rgb [width * height];

  while (infile.read(reinterpret_cast<char*>(in), width * height * sizeof(rgb))) {
//     for (int benchmark=0; benchmark<10; benchmark++) {
    for (unsigned y=0; y<height; y++) {
      if (y > enlarge)
        popRow(rows, recent, minCount, complete, incomplete);
      recent.push_front(list<list<region>::iterator>());
      auto &mostRecent = recent.front();
      rows.push_front(vector<list<region>::iterator>());
      auto &row = rows.front();
      row.reserve(width);
      unsigned ass = rows.size();
      for (unsigned x=0; x<width; x++) {
        rgb px = in[x + y*width];
        for (const threshold t : thresholds) {
          if (t.lor <= px.r && px.r <= t.hir && t.log <= px.g && px.g <= t.hig &&
              t.lob <= px.b && px.b <= t.hib) {
            for (unsigned i=0; i<=enlarge; i++) {
              for (unsigned xi=max(enlarge, x)-enlarge; xi<=x+enlarge && xi<width; xi++) {
                if (i==0 && xi == x)
                  break;
                auto foo = rows[i][xi];
                if (rows[i][xi]->count && rows[i][xi]->name == t.name) { //pixel matches a region
                  if (row.size() <= x) {  //pixel has not yet been connected to a region
                    if (i > 0) {
                      mostRecent.push_back(rows[i][xi]);
                      auto tmp = rows[i][xi]->it;
                      rows[i][xi]->it = --mostRecent.end();
                      recent[i].erase(tmp);
                      row.push_back(rows[i][xi]);
                      a++;
//                       cout << "b " << x << ' ' << y << ' ' << xi << ' ' << i << endl;
                    } else {  //already on the current row
                      row.push_back(rows[i][xi]);
                      b++;
                    }
                    row[x]->count++;
                    row[x]->xc += x;
                    row[x]->yc += y;
                    row[x]->x0 = min(row[x]->x0, x);
                    row[x]->y0 = min(row[x]->y0, y);
                    row[x]->x1 = max(row[x]->x1, x);
                    row[x]->y1 = max(row[x]->y1, y);
                    row[x]->x0y0 = min(row[x]->x0y0, x+y);
                    row[x]->x0y1 = min(row[x]->x0y1, x-y+height);
                    row[x]->x1y0 = max(row[x]->x1y0, x-y+height);
                    row[x]->x1y1 = max(row[x]->x1y1, x+y);
                  } else if(row[x]->id != rows[i][xi]->id) { //combine regions (current pixel has already been added
                    row[x]->id = min(row[x]->id, rows[i][xi]->id);
                    rows[i][xi]->id = row[x]->id;
                    row[x]->xc += rows[i][xi]->xc;
                    row[x]->yc += rows[i][xi]->yc;
                    row[x]->x0 = min(row[x]->x0, rows[i][xi]->x0);
                    row[x]->y0 = min(row[x]->y0, rows[i][xi]->y0);
                    row[x]->x1 = max(row[x]->x1, rows[i][xi]->x1);
                    row[x]->y1 = max(row[x]->y1, rows[i][xi]->y1);
                    row[x]->x0y0 = min(row[x]->x0y0, rows[i][xi]->x0y0);
                    row[x]->x0y1 = min(row[x]->x0y1, rows[i][xi]->x0y1);
                    row[x]->x1y0 = max(row[x]->x1y0, rows[i][xi]->x1y0);
                    row[x]->x1y1 = max(row[x]->x1y1, rows[i][xi]->x1y1);
                    row[x]->count += rows[i][xi]->count;
                    rows[i][xi]->count = 0;
                    c++;
                  }
                }
              }
            }
            if (row.size() <= x) {  //made it to the current pixel and no connections have been found
//               cout << "a " << idNext.load() << ' ' << x << ' ' << y << ' ' << t.name << ' ' << rows[1][x]->name << ' ' << rows[1][x]->id << endl;
              region r = {x, y, x, y, x, y, x+y, x-y+height, x-y+height, x+y, 1, idNext++, t.name, mostRecent.end()};
              incomplete.push_back(r);
              mostRecent.push_back(--incomplete.end());
              incomplete.back().it = --mostRecent.end();
              row.push_back(--incomplete.end());
            }
            break;
          }
        }
        if (row.size() <= x) {
          region r = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, "", mostRecent.end()};
          static list<region> emptyRegion(1, r);
          row.push_back(emptyRegion.begin());
        }
      }
    }
    while (rows.size())
      popRow(rows, recent, minCount, complete, incomplete);
//     }
  }

  cout << idNext.load()-1 << ' ' << a << ' ' << b << ' ' << c << ' ';
  c = 0;
  for (auto &i : complete)
    c += i.count;
  cout << c << endl;
  for (auto &i : complete)
    cout << i.id << ' ' << i.count << ' ' << i.name << ' ' << (i.xc/i.count) << ' ' << (i.yc/i.count) << ' ' << i.x0 << ' ' << i.y0 << ' ' << i.x1 << ' ' << i.y1 << ' ' << endl;

  infile.close();
  return 0;
}
