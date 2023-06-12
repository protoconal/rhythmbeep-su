#include <SD.h>
#include <LiquidCrystal.h>
#include <TMRpcm.h>

TMRpcm player;
LiquidCrystal lcd_1(2, 3, 4, 5, 6, 7);

// queue config
const int PROGMEM maxQueueDepth = 4;
unsigned int beatQueue[maxQueueDepth][3];
unsigned int beatDisplay[32];

// constant configured environmentals
const int PROGMEM speakerPin = 9;
const int PROGMEM knobPin = A1;
const int PROGMEM buttonPin = A0;
const int PROGMEM clockPin = A5;
const int PROGMEM SD_CS_PIN = 10;
const int PROGMEM commonFPS = 10;

// WOKWI/DEBUG controls
const bool WOKWI = true;
const bool debugEndingScreen = false; // dies aat 30 sec

// defaults for where to get check
const String songCountFileName = "songcont.txt";
const String songNamesFileName = "songindx.txt";

// file reading global
File workingFile;

// beatMap variables
String songName; // retrieved song name
int songIndex = -1; // song index

// file structure
// songcont.txt == count of songs available
// songname.txt == index of songs available

// beat structure -- stored in progmem
// sample song : 1.b8t
// 100, 4, 100    // start ms, display position, expire ms
// 55555, 55555, 55555    // end signal

long initTimeStamp = 0;
/*
  void setup()

  - the Arduino setup function that runs once. it’s responsible for initializing inputs and setting up libraries
        - does basic IO declaration
        - sets up the LCD, SD card and TMRpcm libraries
        - offloads other configuration options to getSongsSelection()
        - starts playback of the song at the end
*/
void setup() {
  // setup in/outs
  pinMode(buttonPin, INPUT);
  pinMode(clockPin, INPUT);
  pinMode(knobPin, INPUT);

  Serial.begin(115200);
  // wait for usb serial to populate
  while (!Serial) {
    yield();
  }

  lcd_1.begin(16, 2); // setup the LCD to display
  lcd_1.clear();

  // splash screen
  lcd_1.print(F("RhythmBeep-su!"));
  lcd_1.setCursor(0, 1);
  lcd_1.print(F("The Game that Beats Back!"));

  delay(1000);
  for (int x = 0; x < 128; x++) {
    delay(50);
    lcd_1.scrollDisplayLeft();
  }
  lcd_1.clear();

  // https://docs.wokwi.com/parts/wokwi-microsd-card
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("SD initialization failed."));
    lcd_1.print("Er: SD Init Fail");
    while (1);
  }
  Serial.println(F("SD initialized"));

  // setup tmrpcm
  player.speakerPin = speakerPin;
  player.quality(1);

  Serial.println(F("Files in the card:"));
  workingFile = SD.open("/");
  printDirectory(workingFile, 0);
  Serial.println();

  getSongSelection();
  player.play(songIndex + ".wav");
  lcd_1.clear();
  initTimeStamp = millis();
}

/*
  void clearLCDLine()

  - responsible for clearing a line on the LCD
        - prints a 16 space string to the line
  - sourced from // https://forum.arduino.cc/t/how-to-delete-line-on-lcd/206905/12
*/
void clearLCDLine(int line) {
  lcd_1.setCursor(0, line);
  lcd_1.print(F("                                    "));
  lcd_1.setCursor(0, line);
}

/*
  void printDirectory(File dir, int numTabs)

  - DEBUG RELIC: responsible for printing to serial the files within a directory.
      // sourced from // https://forum.arduino.cc/t/error-when-sd-card-contains-more-than-10-files/629753
*/
void printDirectory(File dir, int numTabs) {
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

// inputLoop variables
bool clockState = HIGH;
bool inputAvailable = true;
int frameInput = 0;
int cursorPos = 0;
/*
  void inputLoop()

  - responsible for all input functions
        - checks if button have been pressed regardless of frame rate, ensuring that inputs are timed with the clock (redundancy to catch d-latch failure events)
        - if an button press has been pressed, increases the current frame's input counter
        - checks the potentiometer and updates the cursor position if it has changed
*/
void inputLoop() {
  clockState = digitalRead(A5);

  // wokwi
  if ((WOKWI || (clockState == HIGH && inputAvailable)) && analogRead(A0) > 50) {
    inputAvailable = false;
    frameInput++;
  }

  // at end of cycle, reset input
  if (clockState == LOW) {
    inputAvailable = true;
  }


  // read and set cursor position
  int newPosition = map(analogRead(knobPin), 680, 0, 0, 31);
  if (cursorPos != newPosition) {
    cursorPos = newPosition;
    updateCursorPosition(cursorPos);
  }
}


/*
  void getSongSelection()

  - large function responsible for getting the choice of song from the user
        - requires index files to properly load (count and actual directory of songs) -- function reports error and hangs if not present
        - retrieves and displays a songName from the SD card onto the LCD, giving the user an interval to confirm the song.
        - if the user does not confirm in time, the next songName is retrieved and displayed
        - if the user does confirm the song, the function exits.
        - the list loops over itself if the end of index has been reached.
        - if two minutes have passed without a selection, the function hangs. // workaround to fix bug -- bad fix we know...
*/
void getSongSelection() {
  workingFile.close();

  // ensure index files exist
  if (SD.exists(songCountFileName) && SD.exists(songNamesFileName)) {
    // open the count
    workingFile = SD.open(songCountFileName);

    Serial.print(songCountFileName + ": "); // logging
    while (!workingFile.available()) {}; // wait until file is ready to be read
    int numberOfLines = workingFile.readStringUntil(F("\n")).toInt(); // get the actual value

    Serial.println(numberOfLines); // logging

    workingFile.close();

    // open the names
    workingFile = SD.open(songNamesFileName, FILE_READ);

    // update lcd
    lcd_1.setCursor(0, 1);
    lcd_1.print(F("Play? or wait."));
    lcd_1.setCursor(0, 0);

    bool flag = true;
    unsigned long futureTimeStamp;
    unsigned long animateTimeStamp;
    unsigned long dieTimeStamp = millis() + 120000; // exit a minute into the future
    while (flag) {
      songIndex++;

      Serial.print(songNamesFileName + ": "); // logging
      delay(100);
      while (!workingFile.available()) {};
      songName = workingFile.readStringUntil('\n');
      Serial.println(songName); // logging

      lcd_1.home();
      clearLCDLine(0);
      lcd_1.print(songName);

      long animateTimeStamp = millis();
      long futureTimeStamp = animateTimeStamp + 4500;
      while (millis() < futureTimeStamp) {
        inputLoop();

        if (millis() - animateTimeStamp > 100) {
          lcd_1.scrollDisplayLeft();
          animateTimeStamp = millis();
        }

        if (frameInput != 0) {
          flag = false;
          break;
        }
      }

      if (songIndex >= numberOfLines) {
        songIndex = -1;
        workingFile.seek(0);
      }

      if (millis() > dieTimeStamp) {
        lcd_1.clear();
        lcd_1.print(F("Sleeping..."));
        lcd_1.setCursor(0,1);
        lcd_1.print(F("Reset to wake..."));
        // do nothing
        while (1) {yield();};
      }
    }

    workingFile.close();

    Serial.print(songName + " : songIndex : ");
    Serial.println(songIndex);

    // setup music file
    setupSongFile();
    lcd_1.blink();
  }
  else {

    lcd_1.clear();
    lcd_1.print(F("Er:No Indx Exists."));
    lcd_1.setCursor(0, 1);
    lcd_1.print(F("Need restore SD."));

    while (1) {
      yield();
    };
  }
}

/*
  void setupSongFile()

  - responsible for intitalizing the beatDisplay and prefetching the first 5 notes
        - resets the display data structure representation by inputting -1
        - prefeteches the first five beats
*/
void setupSongFile() {

  // clear display
  for (int i = 0; i < 32; i++) {
    beatDisplay[i] = -1;
  }
  // pre-fill 5 notes
  for (int i = 0; i < maxQueueDepth; i++) {
    queueBeat();
  }

}

// actual beats
const int beatLength[] = {370, 370, 341, 0, 0, 0};
const unsigned int beatMaps[][380][3] PROGMEM = {
  { // beatMap 0
  {479,1,579},
 {587,3,687},
 {703,5,803},
 {824,7,924},
 {941,9,1041},
 {1062,11,1162},
 {1183,13,1283},
 {1301,15,1401},
 {1416,2,1591},
 {1538,4,1688},
 {1657,6,1782},
 {1775,8,1875},
 {1891,10,1991},
 {2007,12,2107},
 {2132,14,2232},
 {2248,1,2348},
 {2607,2,2657},
 {2650,3,2700},
 {2687,4,2737},
 {2957,5,3007},
 {2991,6,3041},
 {3023,7,3073},
 {3274,9,3374},
 {3306,11,3356},
 {3335,12,3385},
 {3564,14,3614},
 {3596,15,3646},
 {3625,1,3825},
 {3714,2,3764},
 {3738,3,3788},
 {3777,4,3827},
 {3849,1,4049},
 {4025,2,4075},
 {4060,3,4110},
 {4092,4,4142},
 {4127,1,4327},
 {4189,2,4289},
 {4217,3,4317},
 {4250,4,4350},
 {4307,5,4407},
 {4514,6,4614},
 {4721,7,4771},
 {4771,8,4821},
 {4985,9,5085},
 {5017,13,5067},
 {5050,14,5100},
 {5082,15,5132},
 {5196,15,5296},
 {5256,17,5306},
 {5464,19,5564},
 {5489,20,5539},
 {5525,21,5575},
 {5553,22,5603},
 {5584,23,5634},
 {5614,24,5664},
 {5646,25,5696},
 {5677,26,5727},
 {5735,28,5785},
 {6171,1,6371},
 {6288,3,6413},
 {6409,5,6534},
 {6528,7,6653},
 {6648,9,6773},
 {6759,11,6884},
 {6881,13,7006},
 {7000,15,7125},
 {7121,17,7246},
 {7243,19,7368},
 {7362,21,7487},
 {7478,23,7603},
 {7598,24,7723},
 {7715,25,7840},
 {7828,26,7953},
 {7953,27,8078},
 {7993,28,8043},
 {8021,29,8071},
 {8168,1,8293},
 {8250,3,8300},
 {8284,4,8334},
 {8318,5,8368},
 {8437,1,8562},
 {8534,3,8584},
 {8565,4,8615},
 {8599,5,8649},
 {8723,7,8848},
 {8787,9,8837},
 {8812,11,8862},
 {8850,13,8900},
 {8906,15,8956},
 {8968,17,9018},
 {8996,19,9046},
 {9025,21,9075},
 {9312,22,9437},
 {9340,25,9390},
 {9375,26,9425},
 {9406,27,9456},
 {9546,1,9746},
 {9631,2,9681},
 {9737,5,9937},
 {9850,7,10050},
 {9971,7,10096},
 {10003,9,10053},
 {10040,11,10090},
 {10102,13,10152},
 {10134,15,10184},
 {10212,17,10262},
 {10243,19,10293},
 {10287,21,10337},
 {10362,23,10412},
 {10478,24,10603},
 {10518,25,10568},
 {10687,1,10887},
 {10802,3,10902},
 {10924,5,11124},
 {10975,6,11025},
 {11043,7,11093},
 {11078,8,11128},
 {11159,9,11209},
 {11196,10,11246},
 {11249,11,11299},
 {11337,12,11387},
 {11368,13,11418},
 {11400,14,11450},
 {11431,15,11481},
 {11465,16,11515},
 {11499,17,11549},
 {11528,18,11578},
 {11562,19,11612},
 {11590,20,11640},
 {11618,21,11668},
 {11652,22,11702},
 {11677,23,11727},
 {11712,24,11762},
 {11743,25,11793},
 {11775,26,11825},
 {11800,27,11850},
 {11824,28,11874},
 {11853,29,11903},
 {11887,30,11937},
 {11940,31,11990},
 {11999,1,12199},
 {12062,2,12162},
 {12115,3,12215},
 {12176,4,12276},
 {12234,5,12284},
 {12293,6,12343},
 {12350,7,12400},
 {12412,8,12462},
 {12470,9,12520},
 {12534,10,12584},
 {12595,11,12645},
 {12653,12,12703},
 {12712,13,12762},
 {12768,14,12818},
 {12831,15,12881},
 {12890,16,12940},
 {12952,17,13002},
 {12987,18,13037},
 {13065,19,13115},
 {13096,20,13146},
 {13180,17,13230},
 {13218,18,13268},
 {13275,19,13325},
 {13333,20,13383},
 {13396,17,13446},
 {13446,18,13496},
 {13481,19,13531},
 {13506,20,13556},
 {13543,17,13593},
 {13599,18,13649},
 {13628,19,13678},
 {13655,20,13705},
 {13687,17,13737},
 {13718,18,13768},
 {13750,19,13800},
 {13781,20,13831},
 {13812,20,13862},
 {13840,19,13890},
 {13874,18,13924},
 {13906,17,13956},
 {13937,16,13987},
 {13968,15,14018},
 {13999,14,14049},
 {14031,13,14081},
 {14062,12,14112},
 {14096,11,14146},
 {14130,10,14180},
 {14162,9,14212},
 {14193,8,14243},
 {14227,7,14277},
 {14259,6,14309},
 {14290,5,14340},
 {14321,4,14371},
 {14352,3,14402},
 {14418,2,14468},
 {14490,1,14540},
 {14555,2,14605},
 {14611,3,14661},
 {14671,4,14721},
 {14725,5,14775},
 {14818,6,14868},
 {14852,7,14902},
 {14874,8,14924},
 {14912,9,14962},
 {14984,10,15034},
 {15052,11,15102},
 {15112,12,15162},
 {15221,13,15421},
 {15671,1,15871},
 {15740,2,15790},
 {15800,3,15850},
 {15850,4,16050},
 {15912,5,15962},
 {15968,6,16018},
 {16025,7,16225},
 {16084,8,16134},
 {16140,9,16190},
 {16206,10,16406},
 {16268,11,16318},
 {16331,12,16381},
 {16390,13,16590},
 {16450,14,16500},
 {16509,15,16559},
 {16571,16,16771},
 {16628,17,16678},
 {16687,18,16737},
 {16718,19,16918},
 {16762,20,16812},
 {16862,21,16962},
 {16915,20,16965},
 {16978,19,17028},
 {17018,18,17068},
 {17068,17,17118},
 {17156,16,17206},
 {17218,15,17268},
 {17275,14,17325},
 {17334,13,17384},
 {17396,12,17446},
 {17456,11,17506},
 {17506,10,17556},
 {17556,9,17606},
 {17621,8,17671},
 {17659,7,17709},
 {17715,6,17765},
 {17815,5,18015},
 {17856,6,17906},
 {17899,7,17949},
 {17990,8,18090},
 {18018,9,18068},
 {18056,10,18106},
 {18087,11,18137},
 {18115,12,18165},
 {18150,13,18200},
 {18181,14,18231},
 {18209,15,18259},
 {18243,16,18293},
 {18265,17,18315},
 {18303,18,18353},
 {18328,19,18378},
 {18415,20,18515},
 {18474,21,18549},
 {18531,22,18606},
 {18593,23,18668},
 {18650,24,18725},
 {18709,25,18784},
 {18765,26,18840},
 {18796,27,18846},
 {18825,28,18875},
 {18856,29,18906},
 {18887,30,18937},
 {18915,31,18965},
 {18946,30,18996},
 {18974,29,19024},
 {19006,28,19056},
 {19037,27,19087},
 {19068,26,19118},
 {19096,25,19146},
 {19128,24,19178},
 {19156,23,19206},
 {19184,22,19234},
 {19215,21,19265},
 {19246,20,19296},
 {19275,19,19325},
 {19303,18,19353},
 {19334,17,19384},
 {19362,16,19412},
 {19393,15,19443},
 {19424,14,19474},
 {19462,13,19512},
 {19534,12,19584},
 {19596,1,19796},
 {19659,3,19759},
 {19721,5,19821},
 {19784,7,19884},
 {19843,9,19943},
 {19903,11,20003},
 {19962,13,20062},
 {20024,15,20124},
 {20081,17,20181},
 {20131,19,20231},
 {20193,21,20293},
 {20253,23,20353},
 {20312,25,20412},
 {20349,27,20399},
 {20396,29,20446},
 {20434,30,20484},
 {20462,31,20512},
 {20490,30,20540},
 {20521,29,20571},
 {20581,28,20631},
 {20643,27,20693},
 {20681,26,20731},
 {20709,25,20759},
 {20734,24,20784},
 {20762,23,20812},
 {20790,22,20840},
 {20818,21,20868},
 {20846,20,20896},
 {20878,19,20928},
 {20909,18,20959},
 {20940,17,20990},
 {20965,16,21015},
 {20996,15,21046},
 {21021,14,21071},
 {21056,13,21106},
 {21084,12,21134},
 {21121,11,21171},
 {21146,10,21196},
 {21178,9,21228},
 {21206,8,21256},
 {21237,7,21287},
 {21265,6,21315},
 {21293,5,21343},
 {21325,4,21375},
 {21381,3,21481},
 {21440,2,21540},
 {21500,1,21600},
 {21556,3,21656},
 {21621,5,21721},
 {21678,7,21778},
 {21737,9,21837},
 {21796,11,21896},
 {21856,13,21956},
 {21915,15,22015},
 {21975,17,22075},
 {22031,19,22131},
 {22090,21,22190},
 {22155,23,22255},
 {22215,25,22315},
 {22271,27,22371},
 {22328,29,22428},
 {22381,31,22481},
 {22409,30,22459},
 {22437,29,22487},
 {22465,28,22515},
 {22487,27,22537},
 {22512,26,22562},
 {22540,25,22590},
 {22571,24,22621},
 {22596,23,22646},
 {22625,22,22675},
 {22656,21,22706},
 {22728,19,22828},
 {22799,17,22899},
 {22834,16,22884},
 {22915,14,23015},
 {22956,13,23006},
 {23028,10,23128},
 {23196,4,23396},
 {23246,2,23346}
  },
  {
{361,1,461}, 
{678,3,878}, 
{1008,5,1208}, 
{1343,7,1543}, 
{1384,9,1484}, 
{1428,11,1528}, 
{1471,13,1571}, 
{1515,15,1615}, 
{1558,17,1658}, 
{1599,19,1699}, 
{1637,21,1737}, 
{1684,23,1784}, 
{1725,25,1825}, 
{1767,27,1867}, 
{1809,29,1909}, 
{1850,31,1950}, 
{1887,29,1987}, 
{1930,27,2030}, 
{1952,26,2002}, 
{1975,25,2025}, 
{2012,24,2112}, 
{2055,23,2155}, 
{2102,21,2202}, 
{2137,19,2237}, 
{2180,17,2280}, 
{2222,15,2322}, 
{2259,13,2359}, 
{2280,12,2330}, 
{2305,11,2355}, 
{2345,10,2445}, 
{2387,8,2487}, 
{2431,6,2531}, 
{2472,4,2572}, 
{2514,2,2614}, 
{2556,4,2656}, 
{2597,6,2697}, 
{2640,8,2740}, 
{2686,10,2786}, 
{2756,11,2856}, 
{2843,13,2943}, 
{2927,15,3027}, 
{3006,17,3106}, 
{3089,19,3189}, 
{3178,21,3278}, 
{3252,23,3352}, 
{3340,25,3440}, 
{3418,27,3518}, 
{3502,29,3602}, 
{3587,31,3687}, 
{3668,29,3768}, 
{3690,28,3740}, 
{3715,27,3765}, 
{3758,26,3858}, 
{3815,24,3915}, 
{3875,22,3975}, 
{3947,20,4047}, 
{4011,18,4111}, 
{4052,16,4152}, 
{4099,14,4199}, 
{4143,12,4243}, 
{4187,10,4287}, 
{4237,8,4337}, 
{4277,6,4377}, 
{4322,4,4422}, 
{4339,3,4389}, 
{4378,2,4428}, 
{4396,1,4446}, 
{4414,2,4464}, 
{4431,3,4481}, 
{4468,5,4518}, 
{4511,7,4561}, 
{4525,8,4575}, 
{4540,9,4590}, 
{4561,10,4611}, 
{4578,11,4628}, 
{4599,12,4649}, 
{4637,13,4737}, 
{4681,15,4781}, 
{4742,17,4842}, 
{4803,19,4903}, 
{4861,21,4961}, 
{4928,23,5028}, 
{4997,25,5097}, 
{5012,24,5062}, 
{5049,23,5099}, 
{5068,22,5118}, 
{5097,21,5147}, 
{5137,19,5187}, 
{5180,17,5230}, 
{5195,16,5245}, 
{5217,15,5267}, 
{5249,14,5299}, 
{5293,13,5343}, 
{5340,11,5390}, 
{5380,9,5430}, 
{5430,7,5480}, 
{5474,5,5524}, 
{5518,3,5568}, 
{5561,1,5611}, 
{5599,3,5649}, 
{5614,4,5664}, 
{5640,5,5690}, 
{5681,6,5731}, 
{5722,7,5772}, 
{5764,9,5814}, 
{5806,11,5856}, 
{5849,13,5899}, 
{5890,15,5940}, 
{5933,17,5983}, 
{5949,18,5999}, 
{5974,19,6024}, 
{6011,20,6061}, 
{6058,22,6108}, 
{6099,24,6149}, 
{6142,26,6192}, 
{6186,28,6236}, 
{6225,30,6275}, 
{6262,28,6312}, 
{6278,26,6328}, 
{6305,25,6355}, 
{6340,24,6390}, 
{6386,23,6436}, 
{6430,21,6480}, 
{6468,19,6518}, 
{6508,17,6558}, 
{6550,15,6600}, 
{6593,13,6643}, 
{6611,14,6661}, 
{6631,15,6681}, 
{6680,16,6730}, 
{6758,18,6908}, 
{6849,20,6999}, 
{6930,24,7080}, 
{7022,28,7172}, 
{7103,24,7253}, 
{7186,20,7336}, 
{7265,16,7415}, 
{7342,12,7492}, 
{7420,8,7570}, 
{7503,4,7653}, 
{7580,8,7730}, 
{7665,12,7815}, 
{7747,16,7897}, 
{7827,20,7977}, 
{7903,24,8053}, 
{8012,28,8162}, 
{8058,30,8108}, 
{8100,28,8150}, 
{8147,26,8197}, 
{8190,24,8240}, 
{8240,22,8290}, 
{8283,20,8333}, 
{8324,18,8374}, 
{8339,17,8369}, 
{8355,16,8385}, 
{8386,15,8436}, 
{8428,13,8478}, 
{8465,11,8515}, 
{8508,9,8558}, 
{8524,8,8554}, 
{8543,7,8573}, 
{8564,6,8594}, 
{8602,5,8652}, 
{8634,3,8684}, 
{8677,1,8727}, 
{8743,3,8793}, 
{8800,5,8850}, 
{8861,7,8911}, 
{8922,9,8972}, 
{8987,11,9037}, 
{9003,12,9033}, 
{9047,13,9077}, 
{9062,14,9092}, 
{9097,15,9127}, 
{9134,17,9164}, 
{9175,18,9205}, 
{9190,19,9220}, 
{9217,20,9247}, 
{9237,21,9267}, 
{9256,22,9286}, 
{9300,24,9350}, 
{9342,26,9392}, 
{9384,28,9434}, 
{9428,30,9478}, 
{9468,28,9518}, 
{9511,26,9561}, 
{9553,24,9603}, 
{9596,22,9646}, 
{9611,21,9661}, 
{9637,20,9687}, 
{9683,19,9733}, 
{9722,17,9772}, 
{9767,15,9817}, 
{9811,13,9861}, 
{9852,11,9902}, 
{9890,9,9940}, 
{9928,7,9978}, 
{9946,6,9996}, 
{9962,5,10012}, 
{10008,4,10058}, 
{10050,2,10100}, 
{10092,4,10142}, 
{10137,6,10187}, 
{10180,8,10230}, 
{10222,10,10272}, 
{10262,12,10312}, 
{10280,13,10330}, 
{10305,14,10355}, 
{10347,15,10397}, 
{10389,17,10439}, 
{10434,19,10484}, 
{10474,21,10524}, 
{10515,23,10565}, 
{10556,25,10606}, 
{10597,27,10647}, 
{10614,28,10664}, 
{10637,29,10687}, 
{10681,30,10731}, 
{10743,28,10793}, 
{10802,26,10852}, 
{10859,24,10909}, 
{10924,22,10974}, 
{11006,20,11056}, 
{11049,18,11099}, 
{11064,17,11114}, 
{11093,16,11143}, 
{11134,15,11184}, 
{11177,13,11227}, 
{11199,12,11249}, 
{11215,11,11265}, 
{11230,10,11280}, 
{11262,9,11312}, 
{11300,8,11350}, 
{11343,7,11393}, 
{11403,5,11453}, 
{11464,3,11514}, 
{11518,1,11568}, 
{11586,3,11636}, 
{11671,5,11721}, 
{11686,6,11736}, 
{11711,7,11761}, 
{11736,8,11786}, 
{11755,9,11805}, 
{11800,10,11850}, 
{11842,13,11892}, 
{11858,14,11908}, 
{11887,15,11937}, 
{11909,16,11959}, 
{11930,17,11980}, 
{11970,18,12020}, 
{12009,20,12059}, 
{12055,22,12105}, 
{12097,24,12147}, 
{12146,26,12196}, 
{12190,28,12240}, 
{12349,30,12449}, 
{12505,26,12605}, 
{12674,22,12774}, 
{12836,18,12936}, 
{13139,10,13339}, 
{13500,2,13700}, 
{13543,3,13643}, 
{13678,5,13778}, 
{13722,7,13822}, 
{13847,9,13947}, 
{13889,11,13989}, 
{14017,13,14117}, 
{14058,15,14158}, 
{14183,17,14283}, 
{14221,19,14321}, 
{14353,21,14453}, 
{14390,23,14490}, 
{14517,25,14617}, 
{14558,27,14608}, 
{14602,29,14652}, 
{14640,31,14690}, 
{14680,29,14730}, 
{14722,27,14772}, 
{14759,25,14809}, 
{14800,23,14850}, 
{14931,21,14981}, 
{14967,19,15017}, 
{15090,17,15140}, 
{15128,15,15178}, 
{15259,13,15309}, 
{15277,12,15327}, 
{15302,11,15352}, 
{15425,10,15475}, 
{15464,9,15514}, 
{15603,8,15653}, 
{15636,7,15686}, 
{15683,6,15733}, 
{15805,5,15855}, 
{15843,4,15893}, 
{15887,3,15937}, 
{15930,2,15980}, 
{15971,1,16021}, 
{16011,2,16061}, 
{16028,3,16078}, 
{16053,4,16103}, 
{16071,5,16121}, 
{16086,6,16136}, 
{16131,8,16181}, 
{16175,10,16225}, 
{16217,12,16267}, 
{16264,14,16314}, 
{16305,16,16355}, 
{16342,18,16392}, 
{16387,20,16437}, 
{16428,22,16478}, 
{16449,23,16499}, 
{16472,24,16522}, 
{16512,25,16562}, 
{16555,27,16605}, 
{16600,29,16650}, 
{16639,27,16689}, 
{16684,25,16734}, 
{16724,23,16774}, 
{16762,21,16812}, 
{16781,19,16831}, 
{16802,20,16852}, 
{16847,18,16897}, 
{16890,16,16940}, 
{16933,14,16983}, 
{16975,12,17025}, 
{17018,10,17068}, 
{17056,8,17106}, 
{17096,6,17146}, 
{17114,5,17164}, 
{17140,4,17190}, 
{17178,3,17228}, 
{17221,5,17271}, 
{17265,7,17315}, 
{17306,9,17356}, 
{17349,11,17399}, 
{17392,13,17442}, 
{17433,15,17483}, 
{17449,16,17499}, 
{17478,17,17528}, 
{17522,19,17572}, 
{17584,17,17634}, 
{17649,15,17699}, 
{17703,13,17753}, 
{17758,11,17808}, 
{17852,9,17902}, 
{17883,8,17933}, 
{17902,7,17952}, 
{17933,8,17983}, 
{17972,9,18022}, 
{18009,8,18059}, 
{18025,7,18075}, 
{18050,6,18100}, 
{18081,7,18131}, 
{18133,8,18183}, 
{18177,9,18227}, 
{18242,11,18292}, 
{18306,13,18356}, 
{18367,15,18417}, 
{18422,17,18472}, 
{18489,19,18539}, 
{18508,20,18558}, 
{18552,19,18602}, 
{18597,18,18647}, 
{18636,20,18686}, 
{18677,24,18727}, 
{18692,23,18742}, 
{18712,22,18762}, 
{18759,20,18809}, 
{18805,25,18855}, 
{18847,15,18897}
  },
  {
    {150,1,250},
{282,3,382},
{422,5,522},
{566,7,666},
{707,9,807},
{742,11,792},
{775,13,825},
{807,15,857},
{845,17,895},
{880,19,930},
{916,21,966},
{951,23,1001},
{991,25,1041},
{1060,27,1110},
{1133,29,1183},
{1201,31,1251},
{1273,29,1323},
{1342,27,1392},
{1411,25,1461},
{1442,23,1492},
{1478,21,1528},
{1511,19,1561},
{1551,17,1601},
{1626,15,1676},
{1698,13,1748},
{1772,11,1822},
{1841,9,1891},
{1911,7,1961},
{1976,5,2026},
{2011,3,2061},
{2044,1,2094},
{2082,3,2132},
{2117,5,2167},
{2189,7,2239},
{2222,9,2272},
{2258,11,2308},
{2325,13,2375},
{2403,15,2453},
{2475,17,2525},
{2545,19,2595},
{2614,21,2664},
{2682,23,2732},
{2728,25,2778},
{2786,27,2836},
{2891,29,2991},
{2930,31,2980},
{2964,29,3014},
{2998,27,3048},
{3032,25,3082},
{3072,23,3122},
{3107,21,3157},
{3139,19,3189},
{3173,17,3223},
{3205,15,3255},
{3247,13,3297},
{3319,11,3369},
{3388,9,3438},
{3455,7,3505},
{3491,5,3541},
{3526,3,3576},
{3560,1,3610},
{3595,3,3645},
{3632,5,3682},
{3667,7,3717},
{3697,9,3747},
{3738,11,3788},
{3772,13,3822},
{3810,15,3860},
{3841,17,3891},
{3885,19,3935},
{3955,21,4005},
{4030,23,4080},
{4100,25,4150},
{4170,27,4220},
{4238,29,4288},
{4273,31,4323},
{4311,29,4361},
{4342,27,4392},
{4383,25,4433},
{4451,23,4501},
{4526,21,4576},
{4593,19,4643},
{4623,17,4673},
{4661,15,4711},
{4719,13,4769},
{4735,11,4785},
{4763,9,4813},
{4825,7,4875},
{4944,5,5044},
{5017,3,5067},
{5086,1,5136},
{5151,3,5201},
{5217,5,5267},
{5288,7,5338},
{5323,9,5373},
{5361,11,5411},
{5433,13,5483},
{5510,15,5560},
{5544,17,5594},
{5582,19,5632},
{5611,21,5661},
{5648,23,5698},
{5744,25,5794},
{5853,27,5953},
{5930,29,5980},
{5998,31,6048},
{6069,29,6119},
{6136,27,6186},
{6210,25,6260},
{6364,23,6464},
{6497,21,6597},
{6638,19,6738},
{6672,17,6722},
{6705,15,6755},
{6738,13,6788},
{6776,11,6826},
{6883,9,6983},
{6988,7,7088},
{7023,5,7073},
{7060,3,7110},
{7128,1,7178},
{7201,3,7251},
{7238,5,7288},
{7270,7,7320},
{7307,9,7357},
{7342,11,7392},
{7445,13,7545},
{7553,15,7653},
{7603,17,7653},
{7635,19,7685},
{7698,21,7748},
{7761,23,7811},
{7826,25,7876},
{7857,27,7907},
{7900,29,7950},
{7980,31,8030},
{8011,29,8061},
{8048,27,8098},
{8085,25,8135},
{8120,23,8170},
{8153,21,8203},
{8191,19,8241},
{8225,17,8275},
{8261,15,8311},
{8294,13,8344},
{8335,11,8385},
{8403,9,8453},
{8475,7,8525},
{8544,5,8594},
{8576,3,8626},
{8614,1,8664},
{8648,3,8698},
{8685,5,8735},
{8717,7,8767},
{8753,9,8803},
{8791,11,8841},
{8828,13,8878},
{8863,15,8913},
{8895,17,8945},
{8969,19,9019},
{9039,21,9089},
{9110,23,9160},
{9144,25,9194},
{9180,27,9230},
{9210,29,9260},
{9248,31,9298},
{9282,29,9332},
{9319,27,9369},
{9348,25,9398},
{9386,23,9436},
{9411,21,9461},
{9448,19,9498},
{9528,17,9578},
{9563,15,9613},
{9600,13,9650},
{9683,11,9733},
{9745,9,9795},
{9814,7,9864},
{9882,5,9932},
{9948,3,9998},
{10019,1,10069},
{10070,3,10120},
{10116,5,10166},
{10268,7,10518},
{10301,9,10551},
{10441,11,10691},
{10473,13,10723},
{10592,15,10842},
{10661,17,10911},
{10730,19,10980},
{10882,21,11132},
{10908,23,11158},
{11067,25,11317},
{11100,27,11350},
{11223,29,11473},
{11255,31,11505},
{11289,29,11539},
{11441,27,11691},
{11569,25,11819},
{11716,23,11966},
{11857,21,12107},
{11889,19,11939},
{11923,17,11973},
{11961,15,12011},
{11997,13,12047},
{12030,11,12080},
{12066,9,12116},
{12103,7,12153},
{12141,5,12191},
{12155,3,12205},
{12167,1,12217},
{12214,3,12264},
{12250,5,12300},
{12286,7,12336},
{12300,9,12350},
{12311,11,12361},
{12364,13,12414},
{12417,15,12467},
{12605,17,12705},
{12663,19,12763},
{12850,21,12950},
{12880,23,12930},
{12919,25,12969},
{12955,27,13005},
{12991,29,13041},
{13076,31,13126},
{13188,29,13288},
{13244,27,13294},
{13273,25,13323},
{13341,23,13391},
{13405,21,13455},
{13450,19,13500},
{13510,17,13560},
{13803,15,13903},
{13832,13,13882},
{13914,11,13964},
{13973,9,14023},
{14010,7,14060},
{14045,5,14095},
{14078,3,14128},
{14116,1,14166},
{14205,3,14255},
{14320,5,14420},
{14370,7,14420},
{14398,9,14448},
{14464,11,14514},
{14544,13,14594},
{14580,15,14630},
{14613,17,14663},
{14642,19,14692},
{14678,21,14728},
{14783,23,14883},
{14888,25,14988},
{14970,27,15070},
{15048,29,15148},
{15138,31,15238},
{15211,29,15311},
{15323,27,15423},
{15394,25,15494},
{15458,23,15558},
{15489,21,15564},
{15532,19,15607},
{15603,17,15678},
{15678,15,15753},
{15708,13,15783},
{15745,11,15820},
{15772,9,15847},
{15813,7,15888},
{15908,5,15983},
{16007,3,16082},
{16058,1,16133},
{16094,3,16169},
{16160,5,16235},
{16232,7,16307},
{16269,9,16344},
{16303,11,16378},
{16336,13,16411},
{16369,15,16444},
{16405,17,16480},
{16441,19,16516},
{16473,21,16548},
{16510,23,16585},
{16548,25,16623},
{16585,27,16660},
{16619,29,16694},
{16657,31,16732},
{16692,29,16767},
{16728,27,16803},
{16766,25,16841},
{16805,23,16880},
{16836,21,16911},
{16873,19,16948},
{16903,17,16978},
{16947,15,17022},
{17047,13,17147},
{17160,11,17260},
{17241,9,17341},
{17361,7,17461},
{17400,5,17450},
{17438,3,17488},
{17469,1,17519},
{17510,3,17560},
{17610,5,17710},
{17716,7,17816},
{17750,9,17800},
{17788,11,17838},
{17851,13,17901},
{17923,15,17973},
{17961,17,18011},
{17994,19,18044},
{18033,21,18083},
{18070,23,18120},
{18173,25,18273},
{18273,27,18373},
{18332,29,18382},
{18363,31,18413},
{18426,29,18476},
{18497,27,18547},
{18535,25,18585},
{18566,23,18616},
{18598,21,18648},
{18636,19,18686},
{18733,17,18833},
{18844,15,18944},
{18908,13,19008},
{18989,11,19089},
{19053,9,19153},
{19111,7,19211},
{19178,5,19278},
{19273,3,19373},
{19341,1,19441},
{19410,3,19510},
{19488,5,19588},
{19548,7,19648},
{19626,9,19726},
{19695,11,19795},
{19764,13,19864},
{19833,15,19933},
{19910,17,20010},
{19976,19,20076},
{20050,21,20150}
  }
};

// queueBeat variables
unsigned int currentBeat[3];
int retrieveBeatCursor = 0;
int setBeatQueueCursor = 0;
/*
  void queueBeat()

  - responsible for retrieving beat information from progmem to the in-memory beat queue
        - copies beat information from progmem to the beatQueue
        - keeps track of cursor position within both the beatQueue and PROGMEM
        - exits when PROGMEM has been exhausted (requires a exit beat to be reached)
  - debug information: prints beat that was retrieved from progmem
*/
void queueBeat() {
  // check that retrieval hasn't been turned off
  if (retrieveBeatCursor == -1 || beatQueue[setBeatQueueCursor][0] != -1) {
    return;
  }

  // copy beat from progmem to memory
  memcpy_P(currentBeat, beatMaps[songIndex][retrieveBeatCursor], sizeof(currentBeat));
  // access beat information information
  unsigned int beatStartMs = currentBeat[0];
  unsigned int beatDisplayPosition = currentBeat[1];
  unsigned int beatExpiryMs = currentBeat[2];
  // logging
  Serial.print(F("retrieved beat: "));
  Serial.print(beatStartMs);
  Serial.print(F(" / "));
  Serial.print(beatDisplayPosition);
  Serial.print(F(" / "));
  Serial.println(beatExpiryMs);

  // check that ending beat hasn't been reached
  if (retrieveBeatCursor > beatLength[songIndex]) {
    retrieveBeatCursor = -1; // signal to exit
    return;
  }
  retrieveBeatCursor++;

  // queue information
  beatQueue[setBeatQueueCursor][0] = beatStartMs;
  beatQueue[setBeatQueueCursor][1] = beatDisplayPosition;
  beatQueue[setBeatQueueCursor][2] = beatExpiryMs;
  setBeatQueueCursor++;

  // reset cursor if at end of queue
  if (setBeatQueueCursor >= maxQueueDepth) {
    setBeatQueueCursor = 0;
  }
}

// beatLoop variables
int dequeueBeatCursor = 0;
int totalBeats = -1;
int expiredBeats = 0;
/*
  void beatLoopProgmem()

  - responsible for managing the display for beats
        - checks if the currently displayed beats has expired and removes them from the display
        - checks the beat queue to determine if its time to display a new beat
  - debug information: prints position that beats were displayed or removed to serial
*/
void beatLoopProgmem() {

  // loop through beatDisplay and check for expired beats
  for (int beatIndex = 0; beatIndex < 32; beatIndex++) {
    if (beatDisplay[beatIndex] == -1) {
      continue;
    }
    if (millis() > (((long) beatDisplay[beatIndex]) * 10 + initTimeStamp)) {
      updateDisplay(beatIndex, " ");

      Serial.print("deleted beat ");
      Serial.print(beatDisplay[beatIndex]);
      Serial.print(" at ");
      Serial.println(beatIndex);

      Serial.print("currentTime ");
      Serial.print(millis());
      Serial.print(" and beatTime ");
      Serial.println(beatDisplay[beatIndex]);

      beatDisplay[beatIndex] = -1;
      expiredBeats++;
    }
  }

  if (beatQueue[dequeueBeatCursor][0] == -1) {
    return;
  }

  // grab note from queue
  unsigned int startMs = abs(beatQueue[dequeueBeatCursor][0]);
  int displayPosition = abs(beatQueue[dequeueBeatCursor][1]);
  unsigned int expiryMs = abs(beatQueue[dequeueBeatCursor][2]);

  // check if time to play next beat and that beat is not empty
  if (millis() > (((long) startMs) * 10 + initTimeStamp)) {
    // free note queue
    beatQueue[dequeueBeatCursor][0] = -1;
    beatQueue[dequeueBeatCursor][1] = -1;
    beatQueue[dequeueBeatCursor][2] = -1;

    dequeueBeatCursor++;
    // reset cursor if at end of queue
    if (dequeueBeatCursor >= maxQueueDepth) {
      dequeueBeatCursor = 0;
    }

    beatDisplay[displayPosition] = expiryMs;

    updateDisplay(displayPosition, "X");
    totalBeats++;

    Serial.print(F("display x at "));
    Serial.println(displayPosition);
  }
}


int inTimeBeats;
/*
  void gameLoop()

  - responsible for game loop logic tied to the frame counter
        - we force ourselves to use a frame counter
        - DEBUG OPTION: die early at 30000ms total runtime
        - handles input, clears the input counter every time it runs (it only runs every frame or so.)
*/
void gameLoop() {
  if (debugEndingScreen && millis() > 30000) {
    player.stopPlayback();
  }

  if (frameInput != 0) {
    Serial.println(F("frame detected input"));
  }

  if (frameInput != 0) {
    // check that there exists a beat
    if (beatDisplay[cursorPos] != -1) {
      // must be displayed beat
      // update display
      updateCursorPosition(cursorPos); // ensure that its there
      lcd_1.print(" ");
      updateCursorPosition(cursorPos); // ENSURE THAT ITS THERE

      // remove from display queue
      beatDisplay[cursorPos] = -1;

      // increment beat score
      inTimeBeats++;
    }
    else {
      // TODO: implement penalty system
    }
  }
}


const int frameTime = 1000 / commonFPS;
long lastFrameTimestamp = 0;
int frames = 0;
int beatAccuracy = 0;
/*
  void loop()

  - arduino function that is responsible for main looping operations
        - calls other functions such as queueBeat(), beatLoopProgmem(), inputLoop(), and gameLoop()
        - checks the time elapsed since the last frame and updates the game state
        - exits game when song has stopped playing
*/
void loop() {
  queueBeat();
  beatLoopProgmem();
  inputLoop();

  int currentTime = millis();
  if ((currentTime - lastFrameTimestamp) > frameTime) {
    //Serial.println(frames);
    lastFrameTimestamp = millis();
    frames++;
    gameLoop();
    frameInput = 0;
  }

  if (!player.isPlaying()) {
    beatAccuracy = ceil((double) inTimeBeats / (double) totalBeats * 100);
    while (1) {
      exitMenu();
    };
  }
}

// exitMenu variables
const char ranks[] PROGMEM = {'F', 'D', 'C', 'B', 'A', 'S'};
int exitMenuCounter = 0;
/*
  void exitMenu()

  - responsible for displaying the statistics of the player at the end of the game
        - uses a switch and counter statement to determine which statistic to display
        - each screen is displayed for two seconds
        - ensures that counter does not point to an non existent command
*/
void exitMenu() {
  lcd_1.clear();

  // sort of unnecessary switch statement, but imagine it's just like a dictionary to do actions
  // i hope its more readable
  switch (exitMenuCounter) {
    case 0:
      lcd_1.print(F("Beats Hit: "));
      break;
    case 1:
      lcd_1.println(F("Total Beats: "));
      break;
    case 2:
      lcd_1.println(F("Missed Beats: "));
      break;
    case 3:
      lcd_1.print(F("Accuracy: "));
      break;
    case 4:
      lcd_1.print(F("Rank: "));
      break;
    case 5:
      lcd_1.print(F("Play again by"));
      break;
  }

  lcd_1.setCursor(0, 1);

  switch (exitMenuCounter) {
    case 0:
      lcd_1.print(inTimeBeats); 
      break;
    case 1:
      lcd_1.print(totalBeats); 
      break;
    case 2:
      lcd_1.print(expiredBeats); 
      break;
    case 3:
      lcd_1.print(beatAccuracy);
      break;
    case 4:
      lcd_1.print(ranks[map(beatAccuracy, 0, 100, 0, 5)]);
      break;
    case 5:
      lcd_1.print(F("resetting game!"));
      break;
  }

  delay(2000);

  exitMenuCounter++;
  if (exitMenuCounter >= 4) {
    exitMenuCounter = 0;
  }
}

/*
  void updateDisplay(int displayIndex, String displayValue)

  - combines two commands into one function
        - asks later defined function to update the cursor position
        - prints the displayValue to the lcd display
*/
void updateDisplay(int displayIndex, String displayString) {
  updateCursorPosition(displayIndex);
  lcd_1.print(displayString);
}

/*
  void updateCursorPosition(int displayIndex)

  - responsible for moving the cursor to the correct position on the lcd display when provided with a its index
        - calculates the row by dividing by 16
        - calculates the column by subtracting the row from the displayIndex
        - updates the cursor position
*/
void updateCursorPosition(int displayIndex) {
  // integer divide to find column
  int lcdRow = displayIndex / 16;
  int lcdCol = displayIndex - (lcdRow * 16);
  lcd_1.setCursor(lcdCol, lcdRow);
}
