# doc-img-extractor
Tool for extracting images from documents like PDF, DJVU, EPUB, DOC, DOCX using system packages. Includes multithreaded OpenCV figure extraction and OCR verification.

> THIS TOOL IS NOT PERFECT. THERE ARE STILL FALSE POSITIVES AND FALSE NEGATIVES. BEWARE.

Features:
*   **Multi-format support:** PDF, DJVU, EPUB, DOC, DOCX.
*   **Smart Extraction:** Renders pages to detect embedded vector figures (OpenCV).
*   **OCR Verification:** Uses Tesseract to distinguish figures from text blocks.
*   **Multithreaded:** Utilizes all CPU cores for faster processing (toggleable).

## Required Packages

To compile and run this tool, you need both **runtime** dependencies (to process files) and **development** libraries (to compile the code).

### Debian, Ubuntu, Linux Mint (Debian-based)

Run the following command to install everything needed:

```bash
sudo apt-get update && sudo apt-get install -y \
    build-essential \
    libfltk1.3-dev \
    libopencv-dev \
    tesseract-ocr \
    libtesseract-dev \
    libleptonica-dev \
    djvulibre-bin \
    libreoffice \
    poppler-utils \
    unzip
```

### Fedora (and other RHEL-based distributions like CentOS, AlmaLinux)

```bash
sudo dnf install -y \
    gcc-c++ \
    fltk-devel \
    opencv-devel \
    tesseract \
    tesseract-devel \
    leptonica-devel \
    djvulibre \
    libreoffice \
    poppler-utils \
    unzip
```

## Compilation

Compile the program using `g++`. We use `pkg-config` to automatically locate the correct library paths and dependencies for FLTK and OpenCV, and explicitly link Tesseract.

```bash
g++ -std=c++17 main.cpp -o a \
    `fltk-config --cflags --ldflags` \
    `pkg-config --cflags --libs opencv4` \
    -ltesseract -llept -pthread
```

**Flags explained:**
*   `-std=c++17`: Ensures C++17 support for multithreading features.
*   `fltk-config --cflags --ldflags`: Automatically handles FLTK dependencies.
*   `pkg-config ... opencv4`: Automatically handles OpenCV dependencies.
*   `-ltesseract -llept`: Links the OCR libraries.
*   `-pthread`: Ensures proper threading support for the application.

## Usage

1.  Run the executable:
    ```bash
    ./a
    ```
2.  Click **"Choose"** to select your documents.
3.  Select an **Output Directory**.
4.  Configure options:
    *   **Enable OpenCV:** Check to render pages and detect figures (slower, more accurate).
    *   **Use OCR:** Check to verify if a region is text or an image (slower).
    *   **Enable Multithreading:** Check to use 100% CPU for rendering.
5.  Click **Start**.