# FeathrPDF - Ultra-Lightweight Native PDF Reader & Editor for Windows (C++ / Direct2D)

> **Author / Developer:** `Gemini Flash 3.8 High` (yes i didnt touch the code)  
> **Platform:** Windows 10 & Windows 11 (64-bit Native C++ Win32 / Direct2D)  
> **Performance:** DirectX GPU hardware-accelerated, offline, ultra-low idle memory footprint, instant launch.

---

## 🌟 Architecture & Key Features

### 1. Direct2D GPU Hardware Acceleration
- **Direct2D Render Target (`d2d1.lib`)**: Fully leverages GPUs via a DirectX 11 Direct2D Device Context.
- **VRAM Texture Mapping**: Decoded PDF page bitmaps are uploaded directly to GPU VRAM as textures. Zooming, smooth scrolling, and panning are processed in hardware via bilinear filtering, completely eliminating stutter and CPU spikes.

### 2. Idle Memory Trimming
- **Built-in Idle Memory Trimmer**: Automatically trims off-screen page caches and reclaims process working set memory during idle periods.
- Maintains an ultra-low idle memory floor even with large documents or high zoom levels.

### 3. Professional Annotation Tools & Color Picker
- **Size & Opacity Sliders**: Fine-tune stroke width (1px - 25px) and opacity (10% - 100%).
- **16 Quick Colors & HEX Input**: Select quick presets or enter any custom HEX code with real-time preview.
- **Professional Color Picker Dialog**:
  - 64-color curated designer palette.
  - HSV color spectrum canvas with luminance slider.
  - RGB inputs with spinners and bidirectional HEX code synchronization.

### 4. Touch / Stroke-Based Eraser & Full Undo/Redo
- **Stroke-Based Eraser (`E`)**: Touching any pen stroke or highlight instantly removes that entire stroke without affecting underlying PDF text or graphics.
- **Undo (`Ctrl + Z`) & Redo (`Ctrl + Y`)**: Unlimited undo and redo history for all drawing, annotation, and page management actions.

### 5. Native Print Engine
- Clean, internationalized Win32 print dialog:
  - `Printer:` Auto-detects and lists all available system printers.
  - `Page range`: Flexible selection between `All pages`, `Selected pages ([N] page/pages)`, or `Pages:`.
  - Smart page range syntax supporting both commas and semicolons (e.g. `1-5 ; 1-5, 8`).
  - `Subset:` Page filtering (`All pages`, `Odd pages only`, `Even pages only`) for easy manual duplex printing.
  - `About to print:` Real-time preview of page count and print sequence.
  - `Print options`: `Reverse print` (prints from last page to first page for face-up output trays), `Paper size:` (A4, Letter, Legal, A3, A5), `Print orientation:` (Portrait / Landscape), `Page per sheet:` (1, 2, 4, 8, 16), and `Copies`.
  - Full compatibility with printers including Canon LBP2900 via Windows GDI CAPT spooler. maybe others too as long as it have drivers installed

### 6. File Extraction & Smart Result Dialogs
- **Extract to PDF**:
  - Exports selected pages into a new PDF document, preserving all vector text and annotations.
- **Extract to JPG**: idk what to describe

### 7. You can add more PDFs into a single file!
- Simply click "Insert PDF" in "Files" option and choose the PDF you want to add. Default add new pdf at the end of the running pdf file.

## 🛠️ Build & Packaging Instructions

### 1. Build Application:
```cmd
build.bat
```
*Builds the main executable `FeathrPDF.exe` and thumbnail provider `pdfthumb.dll` with `-O2` optimizations.*

### 2. Package Setup Wizard:
```cmd
build_installer.bat
```
*Generates the standalone installer `FeathrPDF_Setup.exe` which does the install the app into your windows device. and no viruses i swear.*
