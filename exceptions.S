.global vector_table
.global sync_exception
.global irq_exception
.global fiq_exception
.global serror_exception

.section .text
.align 11

vector_table:
    b sync_exception
    .align 7
    b irq_exception
    .align 7
    b fiq_exception
    .align 7
    b serror_exception
