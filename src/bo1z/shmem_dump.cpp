// Quick shared-memory probe: reads Local\RE4Z_BO1 and reports header + a few stats.
#include <windows.h>
#include <cstdint>
#include <cstdio>

struct H { uint32_t magic, w, h, stride; volatile uint32_t seq; uint32_t pad[11]; };

int main() {
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, "Local\\RE4Z_BO1");
    if (!m) { printf("shared mem NOT found (%lu)\n", GetLastError()); return 1; }
    uint8_t* p = (uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    if (!p) { printf("MapViewOfFile failed\n"); return 1; }
    H* hdr = (H*)p;
    printf("magic=%08X w=%u h=%u stride=%u seq=%u\n",
           hdr->magic, hdr->w, hdr->h, hdr->stride, (uint32_t)hdr->seq);
    if (hdr->magic == 0x5A314F42 && hdr->w && hdr->h) {
        uint32_t x = hdr->w / 2, y = hdr->h / 2;
        uint8_t* px = p + sizeof(H) + (size_t)y * hdr->stride + x * 4;
        printf("center px B=%u G=%u R=%u A=%u\n", px[0], px[1], px[2], px[3]);
        int opaque = 0, total = 0;
        for (uint32_t yy = 0; yy < hdr->h; yy += 40)
            for (uint32_t xx = 0; xx < hdr->w; xx += 40) {
                uint8_t* q = p + sizeof(H) + (size_t)yy * hdr->stride + xx * 4;
                if (q[3] > 0) opaque++;
                total++;
            }
        printf("opaque ratio = %d/%d\n", opaque, total);
    }
    return 0;
}
