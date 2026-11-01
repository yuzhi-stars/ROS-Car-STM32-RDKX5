/* Compile the project's actual I2C_ITError body, extracted by run_tests.ps1.
 * Only peripheral registers and the surrounding HAL services are simulated. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"I2C HAL FAIL %d: %s\n",__LINE__,#c); std::exit(1); } } while(0)
enum HAL_StatusTypeDef { HAL_OK, HAL_ERROR };
enum HAL_I2C_StateTypeDef { HAL_I2C_STATE_READY, HAL_I2C_STATE_BUSY_TX,
    HAL_I2C_STATE_BUSY_RX, HAL_I2C_STATE_LISTEN=8, HAL_I2C_STATE_ABORT=16 };
enum HAL_I2C_ModeTypeDef { HAL_I2C_MODE_NONE, HAL_I2C_MODE_MASTER, HAL_I2C_MODE_MEM };
enum { HAL_DMA_STATE_READY, HAL_DMA_STATE_BUSY };
enum { I2C_CR1_POS=1, I2C_CR1_PE=2, I2C_CR2_DMAEN=4,
    I2C_FLAG_RXNE=8, I2C_IT_EVT=16, I2C_IT_BUF=32, I2C_IT_ERR=64 };
enum { I2C_STATE_NONE, I2C_NO_OPTION_FRAME=1 };
enum { HAL_I2C_ERROR_NONE=0, HAL_I2C_ERROR_BERR=1, HAL_I2C_ERROR_ARLO=2,
    HAL_I2C_ERROR_AF=4, HAL_I2C_ERROR_OVR=8 };
enum { RESET, SET };
struct I2C_HandleTypeDef;
struct DMA_HandleTypeDef {
    unsigned int State{};
    void (*XferAbortCallback)(DMA_HandleTypeDef *){};
    I2C_HandleTypeDef *Parent{};
};
struct Registers { uint32_t CR1{},CR2{},DR{},flags{}; };
struct I2C_HandleTypeDef {
    HAL_I2C_StateTypeDef State{};
    HAL_I2C_ModeTypeDef Mode{};
    Registers *Instance{};
    uint32_t PreviousState{},ErrorCode{},XferCount{},XferOptions{};
    DMA_HandleTypeDef *hdmatx{},*hdmarx{};
    uint8_t *pBuffPtr{};
    void (*AbortCpltCallback)(I2C_HandleTypeDef *){};
    void (*ErrorCallback)(I2C_HandleTypeDef *){};
    void (*ListenCpltCallback)(I2C_HandleTypeDef *){};
};
#define READ_BIT(reg,bit) ((reg)&(bit))
#define __HAL_I2C_DISABLE(i2c) ((i2c)->Instance->CR1 &= ~I2C_CR1_PE)
#define __HAL_I2C_GET_FLAG(i2c,flag) (((i2c)->Instance->flags & (flag)) != 0 ? SET : RESET)
#define __HAL_I2C_DISABLE_IT(i2c,irq) ((i2c)->Instance->CR2 &= ~(irq))
static unsigned int errors,aborts,dma_aborts,listens;
static void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *) { ++errors; }
static void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *) { ++aborts; }
static void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *) { ++listens; }
static void I2C_DMAAbort(DMA_HandleTypeDef *dma)
{
    dma->Parent->State=HAL_I2C_STATE_READY;
    dma->Parent->Mode=HAL_I2C_MODE_NONE;
    HAL_I2C_ErrorCallback(dma->Parent);
}
static HAL_StatusTypeDef HAL_DMA_Abort_IT(DMA_HandleTypeDef *dma)
{
    ++dma_aborts;
    if (dma->State!=HAL_DMA_STATE_BUSY) return HAL_ERROR;
    dma->State=HAL_DMA_STATE_READY;
    dma->XferAbortCallback(dma);
    return HAL_OK;
}

#include "i2c_error_under_test.inc"

int main()
{
    Registers registers{};
    I2C_HandleTypeDef i2c{};
    DMA_HandleTypeDef tx{},rx{};
    uint8_t buffer=0xA5;
    i2c.Instance=&registers; i2c.hdmatx=&tx; tx.Parent=rx.Parent=&i2c;
    i2c.pBuffPtr=&buffer;
    i2c.ErrorCallback=HAL_I2C_ErrorCallback;
    i2c.AbortCpltCallback=HAL_I2C_AbortCpltCallback;
    i2c.ListenCpltCallback=HAL_I2C_ListenCpltCallback;
    auto prepare=[&]() {
        registers.CR1=I2C_CR1_PE;
        registers.CR2=I2C_CR2_DMAEN|I2C_IT_EVT|I2C_IT_BUF|I2C_IT_ERR;
        i2c.State=HAL_I2C_STATE_BUSY_TX; i2c.Mode=HAL_I2C_MODE_MASTER;
        i2c.ErrorCode=HAL_I2C_ERROR_BERR; i2c.XferCount=0;
    };
    prepare(); I2C_ITError(&i2c); /* TX completed, RX handle absent, late I2C error. */
    CHECK(errors==1 && dma_aborts==0 && i2c.State==HAL_I2C_STATE_READY);
    CHECK(i2c.Mode==HAL_I2C_MODE_NONE && !(registers.CR1&I2C_CR1_PE));
    CHECK(registers.CR2==0 && buffer==0xA5);
    prepare(); i2c.State=HAL_I2C_STATE_ABORT; I2C_ITError(&i2c);
    CHECK(aborts==1 && errors==1 && i2c.ErrorCode==HAL_I2C_ERROR_NONE);
    prepare(); tx.State=HAL_DMA_STATE_BUSY; I2C_ITError(&i2c);
    CHECK(errors==2 && dma_aborts==1 && tx.State==HAL_DMA_STATE_READY);
    prepare(); i2c.hdmatx=nullptr; i2c.hdmarx=&rx;
    i2c.State=HAL_I2C_STATE_BUSY_RX; rx.State=HAL_DMA_STATE_BUSY; I2C_ITError(&i2c);
    CHECK(errors==3 && dma_aborts==2 && rx.State==HAL_DMA_STATE_READY);
    prepare(); i2c.hdmarx=nullptr; I2C_ITError(&i2c);
    CHECK(errors==4 && dma_aborts==2 && listens==0);
    std::puts("PASS actual HAL I2C error body: TX-only late error, abort completion, TX/RX abort and null handles");
}
