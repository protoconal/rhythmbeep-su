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
const int beatLength[] = {368, 0, 0, 0, 0, 0};
const unsigned int beatMaps[][400][3] PROGMEM = {
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
  {},
  {}
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
