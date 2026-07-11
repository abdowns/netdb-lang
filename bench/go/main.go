// go run ./bench/go -n 5000000
package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"math/rand"
	"time"
)

// must match schema Packet's layout in examples/packets.nql
const stride = 32

func synthesize(n int, seed int64) []byte {
	rng := rand.New(rand.NewSource(seed))
	buf := make([]byte, n*stride)
	ports := []uint16{80, 443, 443, 443, 22, 53, 53, 8080, 3306, 5432, 6379, 3389, 23, 4444}
	protos := []uint8{6, 6, 6, 6, 17, 17, 1}
	for i := 0; i < n; i++ {
		r := buf[i*stride:]
		binary.LittleEndian.PutUint64(r[0:], uint64(time.Now().UnixMilli()))
		binary.LittleEndian.PutUint32(r[8:], rng.Uint32())
		binary.LittleEndian.PutUint32(r[12:], rng.Uint32())
		binary.LittleEndian.PutUint16(r[16:], ports[rng.Intn(len(ports))])
		binary.LittleEndian.PutUint16(r[18:], ports[rng.Intn(len(ports))])
		r[20] = protos[rng.Intn(len(protos))]
		r[21] = 0x10
		l := uint32(40 + rng.Intn(200))
		if rng.Intn(10) == 0 {
			l = 1000 + uint32(rng.Intn(64535))
		}
		binary.LittleEndian.PutUint32(r[24:], l)
	}
	return buf
}

func countSuspicious(buf []byte, n int) uint64 {
	var c uint64
	for i := 0; i < n; i++ {
		r := buf[i*stride:]
		dst := binary.LittleEndian.Uint32(r[12:])
		dport := binary.LittleEndian.Uint16(r[18:])
		proto := r[20]
		l := binary.LittleEndian.Uint32(r[24:])
		if ((dport == 22 || dport == 23 || dport == 3389 || dport == 4444) ||
			dst&0xFF000000 == 185<<24) && proto == 6 && l > 64 {
			c++
		}
	}
	return c
}

func main() {
	n := flag.Int("n", 5000000, "record count")
	reps := flag.Int("reps", 5, "repetitions (best reported)")
	flag.Parse()

	buf := synthesize(*n, 42)
	matches := countSuspicious(buf, *n)
	best := time.Duration(1 << 62)
	for r := 0; r < *reps; r++ {
		t0 := time.Now()
		if countSuspicious(buf, *n) != matches {
			panic("unstable count")
		}
		if d := time.Since(t0); d < best {
			best = d
		}
	}
	ms := float64(best.Microseconds()) / 1000.0
	fmt.Printf("Go baseline: %.2f ms, %.1f M rec/s, %.2f ns/rec, %d matches\n",
		ms, float64(*n)/best.Seconds()/1e6,
		float64(best.Nanoseconds())/float64(*n), matches)
}
