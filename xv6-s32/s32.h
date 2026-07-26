void csr_write(unsigned int id, unsigned int val);
unsigned int csr_read(unsigned int id);
void tlb_flush(void);
void eret(void);
void sysenter(unsigned int num);

#define CSR_STATUS    0
#define CSR_SATP      1
#define CSR_EVEC      2
#define CSR_EPC       3
#define CSR_EDATA     4
#define CSR_COUNT     5

#define STATUS_KM     0x00000001
#define STATUS_UM     0x00000002
#define STATUS_KIE    0x00000004
#define STATUS_UIE    0x00000008

#define PRIV_KERNEL   1
#define PRIV_USER     0

static unsigned int r_status(void)  { return csr_read(CSR_STATUS); }
static unsigned int r_satp(void)    { return csr_read(CSR_SATP); }
static unsigned int r_evec(void)    { return csr_read(CSR_EVEC); }
static unsigned int r_epc(void)     { return csr_read(CSR_EPC); }
static unsigned int r_edata(void)   { return csr_read(CSR_EDATA); }
static unsigned int r_count(void)   { return csr_read(CSR_COUNT); }

static void w_status(unsigned int v) { csr_write(CSR_STATUS, v); }
static void w_satp(unsigned int v)   { csr_write(CSR_SATP, v); }
static void w_evec(unsigned int v)   { csr_write(CSR_EVEC, v); }
static void w_epc(unsigned int v)    { csr_write(CSR_EPC, v); }
static void w_edata(unsigned int v)  { csr_write(CSR_EDATA, v); }
static void w_count(unsigned int v)  { csr_write(CSR_COUNT, v); }

static void intr_on(void) {
    unsigned int s = r_status();
    w_status(s | STATUS_KIE | STATUS_UIE);
}

static void intr_off(void) {
    unsigned int s = r_status();
    w_status(s & ~(STATUS_KIE | STATUS_UIE));
}

static int intr_get(void) {
    unsigned int s = r_status();
    return (s & STATUS_KIE) != 0;
}

static void w_stvec(unsigned int v) { w_evec(v); }

static void sfence_vma(void) { tlb_flush(); }
