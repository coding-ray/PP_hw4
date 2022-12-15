#include <pthread.h>   // Pthread related
#include <sys/time.h>  // gettimeofday(), timersub()
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>   // std::setprecision()
#include <iostream>  // std::cout, std::fixed
#include <string>

#include "bmp.h"

using namespace std;

// 定義平滑運算的次數
#define NSmooth 100

// File names
#define INPUT_FILENAME "input.bmp"
#define OUTPUT_FILENAME "output.bmp"

// Number of threads
#define THREAD_COUNT 10

/*********************************************************/
/*變數宣告：                                             */
/*  bmpHeader    ： BMP檔的標頭                          */
/*  bmpInfo      ： BMP檔的資訊                          */
/*  **BMPSaveData： 儲存要被寫入的像素資料               */
/*  **BMPData    ： 暫時儲存要被寫入的像素資料           */
/*********************************************************/
BMPHEADER bmpHeader;
BMPINFO bmpInfo;
RGBTRIPLE **BMPSaveData = NULL;
RGBTRIPLE **BMPData = NULL;
pthread_cond_t condVar[2] = {PTHREAD_COND_INITIALIZER,
                             PTHREAD_COND_INITIALIZER};
pthread_mutex_t completeMutex[2] = {PTHREAD_MUTEX_INITIALIZER,
                                    PTHREAD_MUTEX_INITIALIZER};
int completedThreadCount;

/*********************************************************/
/*函數宣告：                                             */
/*  readBMP    ： 讀取圖檔，並把像素資料儲存在BMPSaveData*/
/*  saveBMP    ： 寫入圖檔，並把像素資料BMPSaveData寫入  */
/*  swap       ： 交換二個指標                           */
/*  **alloc_memory： 動態分配一個Y * X矩陣               */
/*********************************************************/
void *smoothHandler(void *rank);
void waitAllThreadsFinishing(long rank);
int readBMP(const char *fileName);  // read file
void smooth(long rank);             // smooth picture
double getTimeDiff(struct timeval timeEnd, struct timeval timeStart);
int saveBMP(const char *fileName);  // save file
void swap(RGBTRIPLE *a, RGBTRIPLE *b);
RGBTRIPLE **alloc_memory(int Y, int X);  // allocate memory

int main() {
  struct timeval timeStart, timeEnd;
  long thread;  // loop variable
  pthread_t *threadHandles =
      (pthread_t *)malloc(THREAD_COUNT * sizeof(pthread_t));

  completedThreadCount = 0;

  // 讀取檔案
  if (readBMP(INPUT_FILENAME))
    cout << "Read file successfully!!" << endl;
  else
    cout << "Read file fails!!" << endl;

  // 動態分配記憶體給暫存空間
  BMPData = alloc_memory(bmpInfo.biHeight, bmpInfo.biWidth);

  // 記錄開始時間
  gettimeofday(&timeStart, NULL);

  // Give the job of smoothing to the other threads
  for (thread = 0; thread < THREAD_COUNT; thread++) {
    pthread_create(&threadHandles[thread], NULL, smoothHandler, (void *)thread);
  }

  // Make sure all threads have completed their own job
  for (thread = 0; thread < THREAD_COUNT; thread++) {
    pthread_join(threadHandles[thread], NULL);
  }

  // 得到結束時間，並印出執行時間
  gettimeofday(&timeEnd, NULL);
  cout << "The execution time: " << fixed << setprecision(3)
       << getTimeDiff(timeEnd, timeStart) << " seconds" << endl;

  // Finish multi-threading
  free(threadHandles);
  pthread_cond_destroy(&condVar[0]);
  pthread_cond_destroy(&condVar[1]);

  // 寫入檔案
  if (saveBMP(OUTPUT_FILENAME))
    cout << "Save file successfully!!" << endl;
  else
    cout << "Save file fails!!" << endl;

  free(BMPData[0]);
  free(BMPSaveData[0]);
  free(BMPData);
  free(BMPSaveData);

  return 0;
}

void *smoothHandler(void *input) {
  long rank = (long)input;

  if (rank == 0) {
    // 把像素資料與暫存指標做交換
    swap(BMPSaveData, BMPData);
  }

  // Make sure that the swapping finished
  waitAllThreadsFinishing(rank);

  // 進行多次的平滑運算
  for (int round = 0; round < NSmooth; round++) {
    // 進行單次平滑運算
    smooth(rank);

    // 等待所有執行序完成
    waitAllThreadsFinishing(rank);

    if (rank == 0) {
      // 把像素資料與暫存指標做交換
      swap(BMPSaveData, BMPData);
    }
    waitAllThreadsFinishing(rank);
  }

  if (rank == 0) {
    // 把像素資料與暫存指標做交換
    swap(BMPSaveData, BMPData);
  }
  waitAllThreadsFinishing(rank);

  return nullptr;
}

void waitAllThreadsFinishing(long rank) {
  pthread_mutex_lock(&completeMutex[rank % 2]);
  completedThreadCount++;
  if (completedThreadCount != THREAD_COUNT) {
    // Wait
    while (pthread_cond_wait(&condVar[rank % 2], &completeMutex[rank % 2]) != 0)
      ;
    pthread_mutex_unlock(&completeMutex[rank % 2]);
  } else {
    // Broadcast that all threads has finished their own job.
    pthread_mutex_lock(&completeMutex[(rank + 1) % 2]);
    completedThreadCount = 0;
    pthread_cond_broadcast(&condVar[0]);
    pthread_cond_broadcast(&condVar[1]);
    pthread_mutex_unlock(&completeMutex[0]);
    pthread_mutex_unlock(&completeMutex[1]);
  }
}

/*********************************************************/
/* 讀取圖檔                                              */
/*********************************************************/
int readBMP(const char *fileName) {
  // 建立輸入檔案物件
  ifstream bmpFile(fileName, ios::in | ios::binary);

  // 檔案無法開啟
  if (!bmpFile) {
    cout << "It can't open file!!" << endl;
    return 0;
  }

  // 讀取BMP圖檔的標頭資料
  bmpFile.read((char *)&bmpHeader, sizeof(BMPHEADER));

  // 判決是否為BMP圖檔
  if (bmpHeader.bfType != 0x4d42) {
    cout << "This file is not .BMP!!" << endl;
    return 0;
  }

  // 讀取BMP的資訊
  bmpFile.read((char *)&bmpInfo, sizeof(BMPINFO));

  // 判斷位元深度是否為24 bits
  if (bmpInfo.biBitCount != 24) {
    cout << "The file is not 24 bits!!" << endl;
    return 0;
  }

  // 修正圖片的寬度為4的倍數
  while (bmpInfo.biWidth % 4 != 0) bmpInfo.biWidth++;

  // 動態分配記憶體
  BMPSaveData = alloc_memory(bmpInfo.biHeight, bmpInfo.biWidth);

  // 讀取像素資料
  // for(int i = 0; i < bmpInfo.biHeight; i++)
  //	bmpFile.read( (char* )BMPSaveData[i],
  // bmpInfo.biWidth*sizeof(RGBTRIPLE));
  bmpFile.read((char *)BMPSaveData[0],
               bmpInfo.biWidth * sizeof(RGBTRIPLE) * bmpInfo.biHeight);

  // 關閉檔案
  bmpFile.close();

  return 1;
}

void smooth(long rank) {
  // Get the picture height
  int base_count = bmpInfo.biHeight / THREAD_COUNT;
  int additional_count = bmpInfo.biHeight % THREAD_COUNT;
  int height_start, height_end;
  if (rank > additional_count)
    height_start = base_count * rank + additional_count;
  else
    height_start = base_count * rank + rank;

  // base_count * rank + ((rank > additional_count) ? additional_count : rank);

  int next_rank = rank + 1;
  if (next_rank > additional_count)
    height_end = base_count * next_rank + additional_count;
  else
    height_end = base_count * next_rank + next_rank;

  // int height_end =
  //     base_count * (rank + 1) +
  //     (((rank + 1) > additional_count) ? additional_count : (rank + 1));

  // 進行平滑運算
  for (int i = height_start; i < height_end; i++)
    for (int j = 0; j < bmpInfo.biWidth; j++) {
      /*********************************************************/
      /*設定上下左右像素的位置                                 */
      /*********************************************************/
      int Top = i > 0 ? i - 1 : bmpInfo.biHeight - 1;
      int Down = i < bmpInfo.biHeight - 1 ? i + 1 : 0;
      int Left = j > 0 ? j - 1 : bmpInfo.biWidth - 1;
      int Right = j < bmpInfo.biWidth - 1 ? j + 1 : 0;
      /*********************************************************/
      /*與上下左右像素做平均，並四捨五入                       */
      /*********************************************************/
      BMPSaveData[i][j].rgbBlue =
          (double)(BMPData[i][j].rgbBlue + BMPData[Top][j].rgbBlue +
                   BMPData[Top][Left].rgbBlue + BMPData[Top][Right].rgbBlue +
                   BMPData[Down][j].rgbBlue + BMPData[Down][Left].rgbBlue +
                   BMPData[Down][Right].rgbBlue + BMPData[i][Left].rgbBlue +
                   BMPData[i][Right].rgbBlue) /
              9 +
          0.5;
      BMPSaveData[i][j].rgbGreen =
          (double)(BMPData[i][j].rgbGreen + BMPData[Top][j].rgbGreen +
                   BMPData[Top][Left].rgbGreen + BMPData[Top][Right].rgbGreen +
                   BMPData[Down][j].rgbGreen + BMPData[Down][Left].rgbGreen +
                   BMPData[Down][Right].rgbGreen + BMPData[i][Left].rgbGreen +
                   BMPData[i][Right].rgbGreen) /
              9 +
          0.5;
      BMPSaveData[i][j].rgbRed =
          (double)(BMPData[i][j].rgbRed + BMPData[Top][j].rgbRed +
                   BMPData[Top][Left].rgbRed + BMPData[Top][Right].rgbRed +
                   BMPData[Down][j].rgbRed + BMPData[Down][Left].rgbRed +
                   BMPData[Down][Right].rgbRed + BMPData[i][Left].rgbRed +
                   BMPData[i][Right].rgbRed) /
              9 +
          0.5;
    }
}

// Get the difference between timeEnd and timeStart in seconds
double getTimeDiff(struct timeval timeEnd, struct timeval timeStart) {
  struct timeval result;
  timersub(&timeEnd, &timeStart, &result);
  return result.tv_sec + result.tv_usec / 1000000.0;
}

/*********************************************************/
/* 儲存圖檔                                              */
/*********************************************************/
int saveBMP(const char *fileName) {
  // 判決是否為BMP圖檔
  if (bmpHeader.bfType != 0x4d42) {
    cout << "This file is not .BMP!!" << endl;
    return 0;
  }

  // 建立輸出檔案物件
  ofstream newFile(fileName, ios::out | ios::binary);

  // 檔案無法建立
  if (!newFile) {
    cout << "The File can't create!!" << endl;
    return 0;
  }

  // 寫入BMP圖檔的標頭資料
  newFile.write((char *)&bmpHeader, sizeof(BMPHEADER));

  // 寫入BMP的資訊
  newFile.write((char *)&bmpInfo, sizeof(BMPINFO));

  // 寫入像素資料
  // for( int i = 0; i < bmpInfo.biHeight; i++ )
  //         newFile.write( ( char* )BMPSaveData[i],
  //         bmpInfo.biWidth*sizeof(RGBTRIPLE) );
  newFile.write((char *)BMPSaveData[0],
                bmpInfo.biWidth * sizeof(RGBTRIPLE) * bmpInfo.biHeight);

  // 寫入檔案
  newFile.close();

  return 1;
}

/*********************************************************/
/* 分配記憶體：回傳為Y*X的矩陣                           */
/*********************************************************/
RGBTRIPLE **alloc_memory(int Y, int X) {
  // 建立長度為Y的指標陣列
  RGBTRIPLE **temp = new RGBTRIPLE *[Y];
  RGBTRIPLE *temp2 = new RGBTRIPLE[Y * X];
  memset(temp, 0, sizeof(RGBTRIPLE) * Y);
  memset(temp2, 0, sizeof(RGBTRIPLE) * Y * X);

  // 對每個指標陣列裡的指標宣告一個長度為X的陣列
  for (int i = 0; i < Y; i++) {
    temp[i] = &temp2[i * X];
  }

  return temp;
}
/*********************************************************/
/* 交換二個指標                                          */
/*********************************************************/
void swap(RGBTRIPLE *a, RGBTRIPLE *b) {
  RGBTRIPLE *temp;
  temp = a;
  a = b;
  b = temp;
}
