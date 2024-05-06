#include <M5Stack.h>
#include <vector>
#include <stdlib.h>
#include <math.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_NeoPixel.h>
#include <VL53L0X.h>

int selects = 0;
int DistanceMode = 0; //距離判定 ON/OFF

int distance = 0;     //距離値
int distance_sum = 0; //  距離の合計(平均化の際に使用可能)
int distance_flag = 0;  //距離判定 がリセットされたか
int GAflag = 0;         //距離によるGA判定

const int DISTANCE_LENGTH = 430;    //距離の閾値 Aは半分

//const int GENE_LENGTH = 52; // 遺伝子のbit数
const int GENE_LENGTH = 44; // 遺伝子のbit数
const int POPULATION_SIZE = 10; // 1世代あたりの個体数
const double MUTATION_RATE = 0.01; // 突然変異率
const int FINAL_GENE = 100;    //評価完了の世代数

int resetflag = 0;  //GAリセットのためのフラグ

VL53L0X sensor; //VL53L0X　"sensor"として操作することを宣言している。

#define NEOPIXEL_PIN 21   // NEOPIXELのデータピン
#define NUM_PIXELS 70      // 1つのNEOPIXELストリップのLED数

#define PIXELS_GA 27      // 1つのNEOPIXELストリップのLED数

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

File file;

String readGAdata;  //SDカードから読み取ったデータ
int SDgeneration = 999; //SDカードから読み取った世代数
std::string NumberGET = ",ABCDEFGHIJ";  //遺伝子番号
String SDGA_array[POPULATION_SIZE] = {};  //SDカードから読み取った遺伝子を格納する配列


#define BUTTON1_PIN 16
#define BUTTON2_PIN 17

// 遺伝子を表す構造体
struct Individual {
    std::string genes;
    double fitness;
};

void setup() {
  // put your setup code here, to run once:

}

// M5Stackの初期化
void initM5Stack() {
    M5.begin();
    M5.Power.begin();

    // ボタンピンの設定
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
  
    strip.begin();
    strip.show(); // Initialize all pixels to 'off'
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(GREEN , BLACK);
    M5.Lcd.setBrightness(100);
    M5.Lcd.setTextSize(1);

    //距離センサ
    sensor.init();
    sensor.startContinuous();  //  連続測定を開始
}

// M5Stackの操作（例：ボタンが押されたかどうかを返す）
bool isButtonPressed() {
    return M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed();
}

// ランダムな遺伝子を生成する関数
std::string generateRandomGene() {
    std::string gene = "";
    for (int i = 0; i < GENE_LENGTH; ++i) {
        gene += (random(10) % 2) ? '1' : '0';
    }
    return gene;
}

// 初期個体群を生成する関数
std::vector<Individual> generateInitialPopulation() {
    std::vector<Individual> population;
    for (int i = 0; i < POPULATION_SIZE; ++i) {
        Individual individual;
        individual.genes = generateRandomGene();
        individual.fitness = 0.0;
        population.push_back(individual);
    }
    return population;
}

// 適応度を計算する関数（ここでは単純に1の数を数える）←評価方法によって要書き換え
void calculateFitness(Individual &individual) {
    int count = 0;
    for (char gene : individual.genes) {
        if (gene == '1') {
            count++;
        }
    }
    individual.fitness = count;
}

// M5Stackのディスプレイに遺伝子と評価値を表示する関数
void displayGenesAndFitness(const Individual &individual) {
//    M5.Lcd.println("Genes: " + individual.genes + " Fitness: " + String(individual.fitness));
    M5.Lcd.print("Genes: ");
    for(int bitcount = GENE_LENGTH-1;bitcount >= 0;bitcount--)
    {
      M5.Lcd.print(individual.genes[bitcount]);
      if((bitcount)%8 == 0){
        M5.Lcd.print(" ");
      }
    }
    M5.Lcd.println("");
    M5.Lcd.print(" Fitness: ");
    M5.Lcd.println(String(individual.fitness));
}

// ルーレット選択を行う関数
Individual rouletteSelection(const std::vector<Individual> &population) {
    double totalFitness = 0.0;
    for (const Individual &individual : population) {
        totalFitness += individual.fitness;
    }
    double r = (double)rand() / RAND_MAX * totalFitness;
    double sum = 0.0;
    for (const Individual &individual : population) {
        sum += individual.fitness;
        if (sum >= r) {
            return individual;
        }
    }
    return population[0];
}

//SDカードからの値を親に格納する関数
Individual ChangeParentFromSD(String SDData) {
    std::string SDGAGenes = "";
    int stringcount = 0;
    Individual ChangeParent;
    for (int i = 0; i < GENE_LENGTH; ++i) {
      SDGAGenes += SDData.substring(stringcount,stringcount+2).charAt(0);
      stringcount = stringcount + 2;
//      Serial.println(childGenes[i]);
    }
//    Serial.println("");
    ChangeParent.genes = SDGAGenes;
    ChangeParent.fitness = 0.0;
    return ChangeParent;
}

// 一様交叉を行う関数
Individual uniformCrossover(const Individual &parent1, const Individual &parent2) {
    std::string childGenes = "";
    for (int i = 0; i < GENE_LENGTH; ++i) {
        if (rand() % 2) {
            childGenes += parent1.genes[i];
        } else {
            childGenes += parent2.genes[i];
        }
    }
    Individual child;
    child.genes = childGenes;
    child.fitness = 0.0;
    return child;
}

// 突然変異を行う関数
void mutate(Individual &individual) {
    for (int i = 0; i < GENE_LENGTH; ++i) {
        if ((double)rand() / RAND_MAX < MUTATION_RATE) {
            individual.genes[i] = (individual.genes[i] == '0') ? '1' : '0';
        }
    }
}
//////////////////////////////////////////////////////////////////////////////////////
//シリアル通信で遺伝子情報を送る関数
//////////////////////////////////////////////////////////////////////////////////////
void serialcommunication(Individual &individual)
{
    for (int i = 0; i < GENE_LENGTH; ++i) {
        Serial.print(individual.genes[i]);
//        Serial.print(",");
    }
}

///////////////////////////////////////////////////////////////////////////////////////
//LEDを光らせるための関数
///////////////////////////////////////////////////////////////////////////////////////
int color_of_wind(char bit1,char bit2,int rate){
  if(bit2 == '0' && bit1 == '0'){
    return 1;
  }else if(bit2 == '0' && bit1 == '1'){
    return 10 + rate;
  }else if(bit2 == '1' && bit1 == '0'){
    return 20 + rate;
  }else if(bit2 == '1' && bit1 == '1'){
    return 30 + rate;
  }
}

///////////////////////////////////////////////////////////////////////////////////////
//SDカード処理
///////////////////////////////////////////////////////////////////////////////////////
//SDカードへの書き込み
void writeData(int generation,std::vector<Individual> population) {
  file = SD.open("/GA.txt", FILE_APPEND);
  //世代数の書き込み
  file.print(generation);
  for(int index = 0; index < POPULATION_SIZE;index++){
    file.print(",");
    file.print(NumberGET[index+1]);
    for(int i = 0;i < GENE_LENGTH;i++){
      file.print(",");
      file.print(population[index].genes[i]);
    }
  }
  file.println("");
  file.close();
}


//SDカードからの読み込み
void readData() {
  String lastLine = "";
      
  if ( SD.exists("/GA.txt")) {                        // ファイルの存在確認
        file = SD.open("/GA.txt", FILE_READ);      // 読み取り専用でファイルを開く
        if (file) {                                           // ファイルが正常に開けた場合
          while (file.available()) {
            lastLine = file.readStringUntil('\n');
          }
          readGAdata = lastLine;
          int commaIndex[POPULATION_SIZE+1];

          for(int index= 0;index < POPULATION_SIZE+1;index++){
              commaIndex[index] = readGAdata.indexOf(NumberGET[index]);
          }
          SDgeneration = readGAdata.substring(0, commaIndex[0]).toInt();
          
          for(int index= 0;index < POPULATION_SIZE-1;index++){
              SDGA_array[index] = readGAdata.substring(commaIndex[index+1]+2, commaIndex[index+2]-1);
          }
          SDGA_array[POPULATION_SIZE-1] = readGAdata.substring(commaIndex[POPULATION_SIZE]+2);
//          Serial.println(readGAdata);
//          Serial.println(SDgeneration);
          for(int index = 0; index < POPULATION_SIZE;index++){
//            Serial.println(SDGA_array[index]); 
          }
          file.close();                                       // ファイルのクローズ
      } else {
        M5.Lcd.println("error opening /TestFile.txt");        // ファイルが開けない場合
        delay(1000);  
      }
   } else {
      M5.Lcd.println("TestFile.txt doesn't exit.");           // ファイルが存在しない場合
      delay(1000);
  }
}

void DeleteSDData() {
  file = SD.open("/GA.txt", FILE_WRITE);
  file.print("");
  file.close();
}

int readdistance(){
  int distotal = 0;
  for(int discount = 0; discount <10;discount++){
    distotal = distotal + sensor.readRangeContinuousMillimeters();
    delay(5);
  }
  distotal = distotal / 10;

  return distotal;
}

//////////////////////////////////////////////////////////////////////////////////////
//メイン関数
//////////////////////////////////////////////////////////////////////////////////////

int main() {
    initM5Stack(); // M5Stackの初期化

    int button1_state = HIGH;
    int button2_state = HIGH;

    // 初期個体群の生成
    std::vector<Individual> population = generateInitialPopulation();

    //SDカードデータが壊れた時の保険
//                  DeleteSDData();
//                  population = generateInitialPopulation();
//                  writeData(0,population);


    // 世代数分ループ
    for (int generation = 0; generation < FINAL_GENE; ++generation) {
          if(generation == 0){
            readData();

            generation = SDgeneration+1;
            if(generation != 1000){
              for(int index = 0;index < POPULATION_SIZE;index++){
                population[index] = ChangeParentFromSD(SDGA_array[index]);
              }
            }else{
              generation = 0;
              writeData(0,population);
            }
          }
          
          M5.Lcd.fillRect(0, 140, 320, 200, WHITE);
          M5.Lcd.setCursor(100, 150);
          M5.Lcd.setTextColor(RED , WHITE);
          M5.Lcd.setTextSize(7);
          M5.Lcd.println(generation);
          M5.Lcd.setTextColor(GREEN , BLACK);
          M5.Lcd.setTextSize(1);
          M5.Lcd.setCursor(0, 0);
          M5.Lcd.print("Generation : ");
          M5.Lcd.println(generation);
          M5.Lcd.setCursor(0, 10);
          M5.Lcd.print("DistanceMode : ");
          M5.Lcd.println(DistanceMode);

        // 2個体ずつ画面表示させ、評価値（ユーザに選択されたら評価値1）を付与するプログラム
        for (int i = 0; i < POPULATION_SIZE; i += 2) {
            // 評価値クリア
            population[i].fitness = 0.0;
            population[i + 1].fitness = 0.0;

            //serial通信(UNREAL ENGINEに遺伝子A,遺伝子Bの信号を送る)
//            Serial.print("A,");
            serialcommunication(population[i]);
//            Serial.print("B,");
            serialcommunication(population[i+1]);
            Serial.println("");

            //LEDを光らせる
            int geneRED1 =0;
            int geneGRN1 =0;
            int geneBLU1 =0;
            int geneRED2 =0;
            int geneGRN2 =0;
            int geneBLU2 =0;

            geneRED1 = color_of_wind(population[i].genes[0],population[i].genes[1],-1);
            geneGRN1 = color_of_wind(population[i].genes[2],population[i].genes[3],0);
            geneBLU1 = color_of_wind(population[i].genes[4],population[i].genes[5],1);
            geneRED2 = color_of_wind(population[i+1].genes[0],population[i+1].genes[1],-1);
            geneGRN2 = color_of_wind(population[i+1].genes[2],population[i+1].genes[3],0);
            geneBLU2 = color_of_wind(population[i+1].genes[4],population[i+1].genes[5],1);
            for (int color = 5; color < 5+PIXELS_GA; color++) {
              strip.setPixelColor(color, strip.Color(1 + geneRED1, 1 + geneGRN1, 1 + geneBLU1));
              strip.setPixelColor(NUM_PIXELS - color, strip.Color(1 + geneRED2, 1 + geneGRN2, 1 + geneBLU2));
            }
              strip.show(); // 設定した色を表示
            
            // 表示用個体A
            M5.Lcd.setCursor(0, 50);  //遺伝子Aの座標
            displayGenesAndFitness(population[i]);
            // 表示用個体B
            M5.Lcd.setCursor(0, 100);  //遺伝子Aの座標
            displayGenesAndFitness(population[i + 1]);

            // 評価値を入力（M5Stack経由で評価された個体に評価値1をつける）
            M5.Lcd.println("Select the better individual (A/B): ");
            while (!isButtonPressed() && DistanceMode == 0) {
                M5.update(); // M5Stackの更新
                // ボタンが押されるのを待機
              //距離を計測
              distance = readdistance();
              M5.Lcd.setCursor(0, 20);
              M5.Lcd.print("Distance : ");
              M5.Lcd.print(distance);
              M5.Lcd.println("  [mm]  ");

              // 外部ボタンの状態を取得
              button1_state = digitalRead(BUTTON1_PIN);
              button2_state = digitalRead(BUTTON2_PIN);
              if(button1_state == LOW || button2_state == LOW){
                break;
              }
            }
            char selectedButton = ' ';
            while(1){
              // 外部ボタンの状態を取得
              button1_state = digitalRead(BUTTON1_PIN);
              button2_state = digitalRead(BUTTON2_PIN);
              
              //距離を計測
              distance = readdistance();
              M5.Lcd.setCursor(0, 20);
              M5.Lcd.print("Distance : ");
              M5.Lcd.print(distance);
              M5.Lcd.println("  [mm]  ");

              if(distance < DISTANCE_LENGTH){
                if(distance_flag == 0){
                  GAflag = 1;
                }
                distance_flag = 1;
              }else{
                distance_flag = 0;
              }
              
              //GA処理
              M5.update(); // M5Stackの更新
              
              //ボタンA長押しで距離の判定切り替え
              if ( M5.BtnA.pressedFor(1000) ) {
                  if(DistanceMode == 1){
                    DistanceMode = 0;
                  }else if(DistanceMode == 0){
                    DistanceMode = 1;
                  }
                  M5.Lcd.setCursor(0, 10);
                  M5.Lcd.print("DistanceMode : ");
                  M5.Lcd.println(DistanceMode);
                  while(1){
                      //ボタンが押されるまで待機
                    while(M5.BtnA.isPressed()){
                      M5.update(); // M5Stackの更新
                    }
                    M5.update(); // M5Stackの更新
                    break;
                  }
              }
              //ボタンB長押しでSDカード初期化
              if ( M5.BtnB.pressedFor(1000) ) {
                  DeleteSDData();
                  population = generateInitialPopulation();
                  writeData(0,population);
                  M5.Power.reset();
              }
              if(DistanceMode == 0){
                if (M5.BtnA.wasReleased() ||  button1_state == LOW) {
                  selectedButton = 'A';
                  break;
                } else if (M5.BtnC.wasReleased() || button2_state == LOW) {
                  selectedButton = 'C';
                  break;
                } else if(M5.BtnB.wasReleased()){
                  readData();
                  resetflag = 1;
                  i = POPULATION_SIZE;
                  break;
                }
              }else if(DistanceMode == 1){
                if (M5.BtnA.wasReleased() == 1 || ((distance <= DISTANCE_LENGTH*0.6) && (GAflag == 1)) || button1_state == LOW) {
                  selectedButton = 'A';
                  GAflag = 0;
                  M5.Lcd.println("SELECTED A");
                  break;
                } else if (M5.BtnC.wasReleased() == 1 || ((distance > DISTANCE_LENGTH*0.6)&&(distance <= DISTANCE_LENGTH)&& (GAflag == 1)) || button2_state == LOW) {
                  selectedButton = 'C';
                  GAflag = 0;
                  M5.Lcd.println("SELECTED B");
                  break;
                } else if(M5.BtnB.wasReleased()){
                  readData();
                  resetflag = 1;
                  i = POPULATION_SIZE;
                  break;
                }
              }
            }

            if (selectedButton == 'A') {
                population[i].fitness = 1.0;
                //選択された方の光の明滅
                for (int blinkLED = 0;blinkLED < 2;blinkLED++){
                  for (int color = 5; color < 5+PIXELS_GA; color++) {
                    strip.setPixelColor(color, strip.Color(0,0,0));
                    strip.setPixelColor(NUM_PIXELS - color, strip.Color(0,0,0));
                  }
                  strip.show(); // 設定した色を表示
                  delay(500);
                  for (int color = 5; color < 5+PIXELS_GA; color++) {
                    strip.setPixelColor(color, strip.Color(1 + geneRED1, 1 + geneGRN1, 1 + geneBLU1));
                    strip.setPixelColor(NUM_PIXELS - color, strip.Color(0,0,0));
                  }
                  strip.show(); // 設定した色を表示
                  delay(300);
                }
            } else if(selectedButton == 'C'){
                population[i + 1].fitness = 1.0;
                //選択された方の光の明滅
                for (int blinkLED = 0;blinkLED < 2;blinkLED++){
                  for (int color = 5; color < 5+PIXELS_GA; color++) {
                    strip.setPixelColor(color, strip.Color(0,0,0));
                    strip.setPixelColor(NUM_PIXELS - color, strip.Color(0,0,0));
                  }
                  strip.show(); // 設定した色を表示
                  delay(500);
                  for (int color = 5; color < 5+PIXELS_GA; color++) {
                    strip.setPixelColor(color, strip.Color(0,0,0));
                    strip.setPixelColor(NUM_PIXELS - color, strip.Color(1 + geneRED2, 1 + geneGRN2, 1 + geneBLU2));
                  }
                  strip.show(); // 設定した色を表示
                  delay(300);
                }
            }

            M5.update(); // M5Stackの更新
        }

        // 次世代個体群の生成
        std::vector<Individual> nextGeneration;
        for (int j = 0; j < POPULATION_SIZE; ++j) {
            // 親を選択
            Individual parent1 = rouletteSelection(population);
            Individual parent2 = rouletteSelection(population);

            // 交叉
            Individual child = uniformCrossover(parent1, parent2);

            // 突然変異
            mutate(child);

            // 次世代個体群に追加
            nextGeneration.push_back(child);
        }

        // 次世代個体群で更新
        population = nextGeneration;

        //リセットが入っていたら(SDカードからリードされていたら)SDカードから読み取っている遺伝子データを最新の遺伝子データとする。
        if(resetflag == 1){
          resetflag = 0;
          generation = SDgeneration;
          for(int index = 0;index < POPULATION_SIZE;index++){
            population[index] = ChangeParentFromSD(SDGA_array[index]);
//            serialcommunication(population[index]);
//            Serial.println("");
          }
        }else{
          //SDカード書き込み
          writeData(generation,population);
        }
    }

    // 最終世代の表示←100世代目の最も評価高い個体
    for (const Individual &individual : population) {
        displayGenesAndFitness(individual);
    }

    M5.Lcd.fillRect(0, 140, 320, 200, WHITE);
    while(1){
      M5.Lcd.setCursor(20, 175);
      M5.Lcd.setTextColor(RED , WHITE);
      M5.Lcd.setTextSize(4);
      M5.Lcd.println("COMPLETE!!");
    }

    return 0;
}

void loop() {
  // put your main code here, to run repeatedly:
     main();
}
