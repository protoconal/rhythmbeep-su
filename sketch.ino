#include <SD.h>
#include <LiquidCrystal.h>
#include <TMRpcm.h>

TMRpcm player;
LiquidCrystal lcd_1(2, 3, 4, 5, 6, 7);

// queue config
const int PROGMEM maxQueueDepth = 4;
int beatQueue[maxQueueDepth][3];
int beatDisplay[32];

// constant configured environmentals
const int PROGMEM speakerPin = 9;
const int PROGMEM knobPin = A1;
const int PROGMEM buttonPin = A0;
const int PROGMEM clockPin = A5;
const int PROGMEM SD_CS_PIN = 10;
const int PROGMEM commonFPS = 10;

// WOKWI/DEBUG controls
const bool WOKWI = true;
const bool debugEndingScreen = true; // dies aat 30 sec

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

/*
  void setup()

  - responsible for initalizing inputs and setting up libraries
        - does basic IO setup
        - sets up LCD, SD card and tmrpcm
        - offloads other functions into getSongsSelection() 
        - starts playback of song
  - sourced from // https://forum.arduino.cc/t/how-to-delete-line-on-lcd/206905/12
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
  void printDirectory()

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
    unsigned int futureTimeStamp;
    unsigned int animateTimeStamp;
    unsigned int dieTimeStamp = millis() + 120000; // exit a minute into the future
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
      delay(1000);

      futureTimeStamp = millis() + 4500;
      animateTimeStamp = futureTimeStamp - 3500;
      while (millis() < futureTimeStamp) {
        inputLoop();

        if (((int) animateTimeStamp - (int) millis()) > 100) {
          lcd_1.scrollDisplayLeft();
          animateTimeStamp += 100;
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
  for (int i = 0; i < 5; i++) {
    queueBeat();
  }

}

// actual beats
const unsigned int beatMaps[][50][3] PROGMEM = {
  { // beatMap 0
    { 2937, 2, 29304},
    { 2937, 3, 29304},
    { 2937, 4, 29304},
    { 2937, 5, 29304},
    { 2937, 6, 29304},
    { 2937, 7, 29304},
    { 2937, 8, 29304},
    { 2937, 9, 29304},
    { 2937, 10, 29304},
    { 2937, 11, 29304},
    { 2937, 12, 29304},
    { 2937, 13, 29304},
    { 2937, 14, 29304},
    { 2937, 15, 29304},
    { 2937, 16, 29304},
    { 2937, 17, 29304},
    { 2937, 18, 29304},
    { 2937, 19, 29304},
    { 2937, 20, 29304},
    { 2937, 21, 29304},
    { 2937, 22, 29304},
    { 2937, 23, 29304},
    { 2937, 24, 29304},
    { 2937, 25, 29304},
    { 2937, 26, 29304},
    { 2937, 27, 29304},
    { 2937, 28, 29304},
    { 2937, 29, 29304},
    { 2937, 30, 29304},
    { 2937, 20, 29304},
    { 55555, 55555, 55555}, // end
  },
  {},
  {},
};

// queueBeat variables
unsigned int currentBeat[3];
int retrieveBeatCursor = 0;
int setBeatQueueCursor = 0;
/*
  void beatLoopProgmem()

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
  int beatStartMs = currentBeat[0];
  int beatDisplayPosition = currentBeat[1];
  int beatExpiryMs = currentBeat[2];
  // logging
  Serial.print(F("retrieved beat: "));
  Serial.print(beatStartMs);
  Serial.print(F(" / "));
  Serial.print(beatDisplayPosition);
  Serial.print(F(" / "));
  Serial.println(beatExpiryMs);

  // check that ending beat hasn't been reached
  if (beatStartMs == 55555 | beatDisplayPosition == 55555 | beatExpiryMs == -9981) {
    Serial.println(F("reached 55555, 55555, 55555"));
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
    if (millis() > beatDisplay[beatIndex]) {
      updateDisplay(beatIndex, " ");

      Serial.print("deleted beat ");
      Serial.print(beatDisplay[beatIndex]);
      Serial.print(" at ");
      Serial.println(beatIndex);

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
  if (millis() > startMs) {
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
int lastFrameTimestamp = 0;
int frames = 0;
int beatAccuracy = 0;
/*
  void loop()

  - responsible for main looping operations
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