# Stage 1 probe: write N bytes of guest memory with sw, then idle.
# Change N (bytes, multiple of 4) and DELAY (idle loop iterations) per run.
.equ N, 16777216
.equ DELAY, 0

.data
buf: .word 0

.text
    la   t0, buf
    li   t1, N
    li   t2, 1
fill:
    sw   t2, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -4
    bnez t1, fill

    li   t3, DELAY
    beqz t3, done
idle:
    addi t3, t3, -1
    bnez t3, idle

done:
    li   a7, 10
    ecall
