# Final-Project-2025

本專題實作一套完整的 JPEG-like 影像編碼與解碼系統，涵蓋從基本的 RGB 分離，
到頻域轉換、熵編碼（Huffman coding）之完整流程。
系統以 C 語言實作 encoder / decoder，並透過 GitHub Actions 進行自動化編譯與執行測試。

---

## 編碼方法總覽

| Method | 內容說明 |
|------|---------|
| Method 0 | RGB 分離與重建（Baseline，無壓縮） |
| Method 1 | RGB → YCbCr → 2D-DCT → Quantization |
| Method 2 | Method 1 + DPCM + ZigZag + RLE |
| Method 3 | Method 2 + Huffman Coding |

---

## 檔案說明

| 檔名 | 說明 |
|---|---|
| `encoder.c` | Encoder 主程式（Method 0–3） |
| `decoder.c` | Decoder 主程式（Method 0–3） |
| `Kimberly.bmp` | 原始輸入影像 |
| `ResKimberly.bmp` | Decoder 還原影像 |
| `Qt_Y.txt / Qt_Cb.txt / Qt_Cr.txt` | Quantization Tables |
| `qF_*.raw` | 量化後 DCT 係數 |
| `eF_*.raw` | Quantization Error |
| `rle_code.txt / rle_code.bin` | RLE 編碼結果 |
| `codebook.txt` | Huffman Codebook |
| `huffman_code.txt / huffman_code.bin` | Huffman Bitstream |
| `.github/workflows/main.yml` | GitHub Actions CI |

---

## 實作進度與工作紀錄

### Phase 1：BMP 與 Method 0
- 解析 BMP Header（含 row padding 與 bottom-up 儲存）
- 實作 RGB channel 分離與重建
- 驗證 decoder 能完整還原原始影像

### Phase 2：Method 1（DCT 與 Quantization）
- 實作 RGB ↔ YCbCr 轉換
- 實作 8x8 2D-DCT / IDCT
- 設計 Quantization Table（Y / Cb / Cr）
- 輸出 quantized coefficients 與 quantization error

### Phase 3：Method 2（RLE）
- 實作 DC component DPCM
- ZigZag 掃描順序設計
- 設計 ASCII 與 Binary RLE 格式
- Decoder 可完整還原影像

### Phase 4：Method 3（Huffman Coding）
- 建立 Huffman codebook（symbol / count / codeword）
- 實作 bitstream 編碼與 padding
- Decoder 成功解碼並還原影像
- 比較 ASCII 與 Binary 壓縮效率

### Phase 5：系統整合與 CI
- 整合 encoder / decoder（Method 0–3）
- 建立 GitHub Actions 自動化編譯與執行測試流程

---

## 編譯指令

```bash
gcc encoder.c -O2 -Wall -lm -o encoder
gcc decoder.c -O2 -Wall -lm -o decoder

# ===== Build =====
gcc encoder.c -O2 -Wall -lm -o encoder
gcc decoder.c -O2 -Wall -lm -o decoder

# ===== Method 0 : RGB Split & Rebuild =====
./encoder 0 Kimberly.bmp R.txt G.txt B.txt dim.txt
./decoder 0 ResKimberly.bmp R.txt G.txt B.txt dim.txt
diff Kimberly.bmp ResKimberly.bmp

# ===== Method 1 : YCbCr + DCT + Quantization =====
./encoder 1 Kimberly.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt \
qF_Y.raw qF_Cb.raw qF_Cr.raw eF_Y.raw eF_Cb.raw eF_Cr.raw

./decoder 1 ResKimberly.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt \
qF_Y.raw qF_Cb.raw qF_Cr.raw eF_Y.raw eF_Cb.raw eF_Cr.raw
diff Kimberly.bmp ResKimberly.bmp

# ===== Method 2 : DPCM + ZigZag + RLE (ASCII) =====
./encoder 2 Kimberly.bmp ascii rle_code.txt
./decoder 2 ResKimberly.bmp ascii rle_code.txt

# ===== Method 2 : DPCM + ZigZag + RLE (Binary) =====
./encoder 2 Kimberly.bmp binary rle_code.bin
./decoder 2 ResKimberly.bmp binary rle_code.bin

# ===== Method 3 : Huffman Coding (ASCII) =====
./encoder 3 Kimberly.bmp ascii codebook.txt huffman_code.txt
./decoder 3 ResKimberly.bmp ascii codebook.txt huffman_code.txt

# ===== Method 3 : Huffman Coding (Binary) =====
./encoder 3 Kimberly.bmp binary codebook.txt huffman_code.bin
./decoder 3 ResKimberly.bmp binary codebook.txt huffman_code.bin

