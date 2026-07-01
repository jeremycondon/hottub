#include "../spa_control.h"
#include "../balboa_frame.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// Wrap a body in a full CRC'd frame for feeding onFrame().
static size_t frame(uint8_t* out, const uint8_t* body, size_t n) {
    return balboa_build_frame(out, 64, body, n);
}

int main() {
    SpaProtocol p;
    uint8_t buf[64];

    // Status frames are parsed regardless of arm state.
    const uint8_t status[] = {0xFF,0xAF,0x13,
        0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
    size_t n = frame(buf, status, sizeof status);
    p.onFrame(buf, n, 1000);
    assert(p.state().currentTempF == 95 && p.state().setTempF == 85);

    // Command queue accepts commands.
    assert(!p.hasPendingCommand());
    p.cmdSetTemp(102);
    assert(p.hasPendingCommand());

    // --- Registration handshake (only when armed) ---
    SpaProtocol q;
    uint8_t tx[32];

    // Disarmed: new-client poll produces nothing.
    const uint8_t newclient[] = {0xFE,0xBF,0x00};
    size_t m = frame(buf, newclient, sizeof newclient);
    q.onFrame(buf, m, 10);
    assert(q.pollTx(tx, sizeof tx) == 0);
    assert(q.regState() == SpaProtocol::Unregistered);

    // Arm, then handshake.
    q.setArmed(true);
    q.onFrame(buf, m, 20);                         // FE BF 00
    size_t rn = q.pollTx(tx, sizeof tx);
    const uint8_t wantReq[] = {0xFE,0xBF,0x01,0x02,0xF1,0x73};
    uint8_t reqFrame[32]; size_t reqLen = balboa_build_frame(reqFrame, 32, wantReq, sizeof wantReq);
    assert(rn == reqLen && memcmp(tx, reqFrame, rn) == 0);
    assert(q.regState() == SpaProtocol::Requesting);

    // Assignment -> ack, channel stored.
    const uint8_t assign[] = {0xFE,0xBF,0x02,0x05};
    m = frame(buf, assign, sizeof assign);
    q.onFrame(buf, m, 30);
    size_t an = q.pollTx(tx, sizeof tx);
    const uint8_t wantAck[] = {0x05,0xBF,0x03};
    uint8_t ackFrame[32]; size_t ackLen = balboa_build_frame(ackFrame, 32, wantAck, sizeof wantAck);
    assert(an == ackLen && memcmp(tx, ackFrame, an) == 0);
    assert(q.regState() == SpaProtocol::Assigned && q.channel() == 0x05);

    // CTS with empty queue -> nothing-to-send.
    const uint8_t cts[] = {0x05,0xBF,0x06};
    m = frame(buf, cts, sizeof cts);
    q.onFrame(buf, m, 40);
    size_t nn = q.pollTx(tx, sizeof tx);
    const uint8_t wantNts[] = {0x05,0xBF,0x07};
    uint8_t ntsFrame[32]; size_t ntsLen = balboa_build_frame(ntsFrame, 32, wantNts, sizeof wantNts);
    assert(nn == ntsLen && memcmp(tx, ntsFrame, nn) == 0);

    // Queue a set-temp, next CTS sends it.
    q.cmdSetTemp(102);
    q.onFrame(buf, m, 50);
    size_t cn = q.pollTx(tx, sizeof tx);
    const uint8_t wantCmd[] = {0x05,0xBF,0x20,102};
    uint8_t cmdFrame[32]; size_t cmdLen = balboa_build_frame(cmdFrame, 32, wantCmd, sizeof wantCmd);
    assert(cn == cmdLen && memcmp(tx, cmdFrame, cn) == 0);
    assert(!q.hasPendingCommand());

    // Disarm resets registration.
    q.setArmed(false);
    assert(q.regState() == SpaProtocol::Unregistered && q.channel() == 0);

    // --- Channel id is capped at 0x2F ---
    SpaProtocol capP;
    capP.setArmed(true);
    const uint8_t nc2[] = {0xFE,0xBF,0x00};
    m = frame(buf, nc2, sizeof nc2);
    capP.onFrame(buf, m, 100); capP.pollTx(tx, sizeof tx);        // -> Requesting
    const uint8_t assignHigh[] = {0xFE,0xBF,0x02,0x40};          // 0x40 > 0x2F
    m = frame(buf, assignHigh, sizeof assignHigh);
    capP.onFrame(buf, m, 110); capP.pollTx(tx, sizeof tx);
    assert(capP.channel() == 0x2F);

    // --- Stray assignment while Unregistered is ignored ---
    SpaProtocol strayP;
    strayP.setArmed(true);
    const uint8_t assign5[] = {0xFE,0xBF,0x02,0x05};
    m = frame(buf, assign5, sizeof assign5);
    strayP.onFrame(buf, m, 120);
    assert(strayP.regState() == SpaProtocol::Unregistered);
    assert(strayP.pollTx(tx, sizeof tx) == 0);

    // --- Disarm mid-handshake clears the pending owe and resets ---
    SpaProtocol midP;
    midP.setArmed(true);
    m = frame(buf, nc2, sizeof nc2);
    midP.onFrame(buf, m, 130);                                   // owe request pending, not yet polled
    midP.setArmed(false);
    assert(midP.regState() == SpaProtocol::Unregistered && midP.channel() == 0);
    assert(midP.pollTx(tx, sizeof tx) == 0);                     // owe cleared -> nothing emitted

    // --- Non-BF frame clears a pending owe (Fix 1: onFrame owe reset) ---
    SpaProtocol nbP;
    nbP.setArmed(true);
    const uint8_t nbNew[] = {0xFE,0xBF,0x00};
    m = frame(buf, nbNew, sizeof nbNew);
    nbP.onFrame(buf, m, 200);                  // owe=OweRequest pending, NOT polled
    const uint8_t nonbf[] = {0x10,0x00,0x00};  // t0=0x00 != 0xBF
    m = frame(buf, nonbf, sizeof nonbf);
    nbP.onFrame(buf, m, 210);                  // must clear the pending owe
    assert(nbP.pollTx(tx, sizeof tx) == 0);

    // --- Encode failure leaves the command queued (Fix 2: dequeue-on-success) ---
    SpaProtocol dqP;
    dqP.setArmed(true);
    m = frame(buf, nbNew, sizeof nbNew);
    dqP.onFrame(buf, m, 300); dqP.pollTx(tx, sizeof tx);       // -> Requesting
    const uint8_t dqAssign[] = {0xFE,0xBF,0x02,0x05};
    m = frame(buf, dqAssign, sizeof dqAssign);
    dqP.onFrame(buf, m, 310); dqP.pollTx(tx, sizeof tx);       // -> Assigned ch=5
    dqP.cmdSetTemp(102);
    const uint8_t dqCts[] = {0x05,0xBF,0x06};
    m = frame(buf, dqCts, sizeof dqCts);
    dqP.onFrame(buf, m, 320);                  // owe=OweCommand
    uint8_t tiny[3];
    assert(dqP.pollTx(tiny, sizeof tiny) == 0);   // 8-byte cmd can't fit in cap=3
    assert(dqP.hasPendingCommand());              // command NOT dequeued on encode failure

    // --- Lost request self-heals: re-request on repeated FE BF 00 while Requesting (Fix 2) ---
    SpaProtocol rrP;
    rrP.setArmed(true);
    const uint8_t rrNc[] = {0xFE,0xBF,0x00};
    m = frame(buf, rrNc, sizeof rrNc);
    rrP.onFrame(buf, m, 400);
    assert(rrP.pollTx(tx, sizeof tx) > 0);              // first request emitted
    assert(rrP.regState() == SpaProtocol::Requesting);
    rrP.onFrame(buf, m, 410);                           // request assumed lost; polled again
    size_t rr = rrP.pollTx(tx, sizeof tx);
    const uint8_t wantReq2[] = {0xFE,0xBF,0x01,0x02,0xF1,0x73};
    uint8_t reqF2[32]; size_t reqL2 = balboa_build_frame(reqF2, sizeof reqF2, wantReq2, sizeof wantReq2);
    assert(rr == reqL2 && memcmp(tx, reqF2, rr) == 0);  // request re-sent
    assert(rrP.regState() == SpaProtocol::Requesting);

    // --- A status broadcast clears any pending owe (Fix 3) ---
    SpaProtocol stP;
    stP.setArmed(true);
    m = frame(buf, rrNc, sizeof rrNc);
    stP.onFrame(buf, m, 500);                           // owe=OweRequest pending, not polled
    const uint8_t stStatus[] = {0xFF,0xAF,0x13,
        0x00,0x03,0x5F,0x0C,0x29,0x00,0x28,0x55,0x00,0x00,0x04,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x02,0x02,0x55,0x00,0x00,0x02,0x3C,0x00,0x00};
    m = frame(buf, stStatus, sizeof stStatus);
    stP.onFrame(buf, m, 510);                           // must clear the pending owe
    assert(stP.pollTx(tx, sizeof tx) == 0);

    printf("ALL TESTS PASSED\n");
    return 0;
}
