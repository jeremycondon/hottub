#include "../spa_reconcile.h"
#include "../spa_control.h"
#include "../spa_state.h"
#include <cassert>
#include <cstdio>

// Build a SpaState with the fields the reconciler reads.
static SpaState st(uint8_t setF, uint8_t p1, bool p2, bool light) {
    SpaState s; s.setTempF = setF; s.pump1 = p1; s.pump2 = p2; s.light = light;
    s.tempKnown = true; return s;
}

int main() {
    // pump2 on: one toggle, then converges when the state reflects it.
    { SpaReconciler r; SpaProtocol p;
      r.setPump2(true);
      r.tick(st(100,0,false,false), p, 1000);   assert(p.pendingCount() == 1);
      r.tick(st(100,0,true, false), p, 1300);   assert(p.pendingCount() == 1); // reached
      r.tick(st(100,0,true, false), p, 1600);   assert(p.pendingCount() == 1); // idle
    }
    // effect pacing: no second command before effect or timeout.
    { SpaReconciler r; SpaProtocol p;
      r.setLight(true);
      r.tick(st(100,0,false,false), p, 1000);   assert(p.pendingCount() == 1); // issue #1
      r.tick(st(100,0,false,false), p, 1200);   assert(p.pendingCount() == 1); // waiting (<2s)
      r.tick(st(100,0,false,false), p, 3100);   assert(p.pendingCount() == 2); // timeout retry
    }
    // pump1 cyclic: off -> high needs two toggles.
    { SpaReconciler r; SpaProtocol p;
      r.setPump1(2);
      r.tick(st(100,0,false,false), p, 1000);   assert(p.pendingCount() == 1);
      r.tick(st(100,1,false,false), p, 1300);   assert(p.pendingCount() == 2); // low seen, go again
      r.tick(st(100,2,false,false), p, 1600);   assert(p.pendingCount() == 2); // high reached
    }
    // give up after MAX with no effect; flag set; new command resets.
    { SpaReconciler r; SpaProtocol p;
      r.setLight(true);
      r.tick(st(100,0,false,false), p, 0);      // #1
      r.tick(st(100,0,false,false), p, 3000);   // #2
      r.tick(st(100,0,false,false), p, 6000);   // #3
      assert(p.pendingCount() == 3);
      r.tick(st(100,0,false,false), p, 9000);   // give up, no issue
      assert(p.pendingCount() == 3);
      assert(r.lightGaveUp());
      r.setLight(true); assert(!r.lightGaveUp());
      r.tick(st(100,0,false,false), p, 12000);  assert(p.pendingCount() == 4);
    }
    // temp absolute re-send then converge.
    { SpaReconciler r; SpaProtocol p;
      r.setTemp(102);
      r.tick(st(100,0,false,false), p, 1000);   assert(p.pendingCount() == 1);
      r.tick(st(102,0,false,false), p, 1300);   assert(p.pendingCount() == 1);
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
