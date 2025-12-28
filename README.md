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
## 心得及感想
在這次 MMSP 期末專題中，透過實作一套完整的 BMP 影像壓縮與還原系統，我對課堂上所學的影像壓縮觀念有了更實際的體會。一開始從最基本的 RGB 拆分與影像重建做起，讓我了解影像資料在電腦中實際儲存與處理的方式；接著在 Method 1 中進行 YCbCr 色彩空間轉換、8×8 區塊切分、DCT 以及量化時，才真正理解為什麼頻域轉換能有效集中能量，進而達到壓縮的效果。之後在 Method 2 與 Method 3 實作 DPCM、ZigZag 掃描、RLE 以及 Huffman Coding 的過程中，讓我感受到資料統計特性對壓縮效率的影響，也發現 encoder 與 decoder 在資料格式設計上必須非常嚴謹，只要有一個地方對不上，還原出來的影像就會出現錯誤。除此之外，這次專題也讓我實際體驗到撰寫完整系統所需要的耐心，特別是在除錯 decoder 以及確認每一個方法都能正確還原影像時花了不少時間。最後透過 GitHub Actions 建立自動化編譯與測試流程，讓我體會到自動化工具在實務開發中的重要性，也讓整個專題流程更加完整，對我之後進行較大型程式專案有很大的幫助。

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


