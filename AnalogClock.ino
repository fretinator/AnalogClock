/***********************************************************************************
Analog clock with LCDWIKI_KBV and RTC_DS1307
**********************************************************************************/
#include <TouchScreen.h>
#include <LCDWIKI_GUI.h> //Core graphics library
#include <LCDWIKI_KBV.h> //Hardware-specific library
#include <lcd_mode.h>
#include <lcd_registers.h>
#include <mcu_8bit_magic.h>
#include <Coordinates.h>
#include <Wire.h>
#include <RTClib.h>

LCDWIKI_KBV mylcd(ILI9486,A3,A2,A1,A0,A4); //model,cs,cd,wr,rd,reset
RTC_DS1307 clock;

char daysOfTheWeek[7][12] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
Coordinates point = Coordinates();

//define some colour values
#define  BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF
#define LIGHT_GRAY 0xA514
#define DARK_GRAY 0x2124

#define SCREEN_BACK_COLOR BLUE
#define CLOCK_HASH_COLOR BLACK
#define CLOCK_FACE_COLOR WHITE
#define HOUR_HAND_COLOR BLACK
#define MINUTE_HAND_COLOR DARK_GRAY
#define INNER_CIRCLE_COLOR GREEN


int curMillis = 0;
int lastMillis = 0;
const int UPDATE_MILLIS = 1000; // Every second
bool firstRun = true;
const int prevDayOfMonth = -1;
const int CLOCK_PADDING = 10;
const int HASH_LINE_SIZE = 16;
const int HASH_RECT_HEIGHT = 16;
const int HASH_RECT_WIDTH = 8;
const int INNER_CIRCLE_RADIUS = 8; 
const int HOUR_HAND_BASE = 16;
const int HOUR_HAND_LENGTH = 60;
const int MINUTE_HAND_BASE = 8;
const int MINUTE_HAND_LENGTH = 100;
const float CIRCLE_RADS = 2 * PI;
const float APPROXIMATION_VALUE = 0.001;
const int DATE_SEPARATION = 60; // 30 pixels below clock
const int DATE_SIZE = 3; // How big to write the date

struct HashPos {
  int x1;
  int y1;
  int x2;
  int y2;
};

struct AnalogClockPos {
  int x;
  int y;
  int r;
  HashPos hash_pos[12];
};

struct HourPos {
  int x1;
  int y1;
  int x2;
  int y2;
  int x3;
  int y3;  
};

struct MinutePos {
  int x1;
  int y1;
  int x2;
  int y2;
  int x3;
  int y3;  
};

HourPos lastHourPos;
MinutePos lastMinutePos;
int lastMinute = -1; // used to know when to redraw hands
int lastDay = -1; // used to know when to redraw date


int screenWidth, screenHeight;

AnalogClockPos clockPos;


bool approximatelyEqual(float f1, float f2) {
  return abs(f1 - f2) < APPROXIMATION_VALUE;
}

void eraseDate() {
  int x1 = 0;
  int y1 = clockPos.x + clockPos.r + (DATE_SEPARATION / 2);
  int x2 = screenWidth - 1;
  int y2 = screenHeight - 1;

  mylcd.Set_Draw_color(SCREEN_BACK_COLOR);
  
  mylcd.Fill_Rectangle(x1, y1, x2, y2);
}

//display date
void drawDate(DateTime now, bool firstTime)
{
  if(!firstTime) {
    eraseDate();
  }
  
  mylcd.Set_Text_Size(DATE_SIZE);
  mylcd.Set_Text_Back_colour(CLOCK_FACE_COLOR);
  mylcd.Set_Text_colour(CLOCK_HASH_COLOR);

  int yPos = clockPos.y + clockPos.r + DATE_SEPARATION;
  int xPos = 3 * CLOCK_PADDING;
  String dateStr;
  String strMonth = String(now.month());
  String strDay = String(now.day());
  
  if(strMonth.length() < 2) {
    strMonth = '0' + strMonth;
  }

  if(strDay.length() < 2) {
    strDay = '0' + strDay;
  }
  
  dateStr = strMonth + "/" + strDay
    + "/" + String(now.year()) + " " + 
    daysOfTheWeek[now.dayOfTheWeek()]; 
  mylcd.Print_String(dateStr, xPos, yPos);
}


void getClockPoint(int r, float phi, int* x, int* y) {
  point.fromPolar(r, phi);
  // The point fromPolar is calculating points based off of a
  // 0,0 origin, but ours is at clockPos.x, clockPos.y,
  // which in a 320 width screen is point 160, 160
  *x = point.getX() + clockPos.x;
  *y = point.getY() + clockPos.y;
}


float getAngleForMinute(int theMinute) {
  float minuteSlice = CIRCLE_RADS / 60; // Angle measurement in each hour

  // For minutes, 15 minutes is 0, so we have to take away 15 minutes

  if(theMinute < 15) {
    theMinute += 60;
  }

  theMinute -= 15; // Rotate my coordiate so 0 is the new "pole"

  return theMinute * minuteSlice;
}

// Float to support fractional hours
// Needed because hour keeps moving slightly while minute hand moves
float getAngleForHour(float theHour) {
  float hourSlice = CIRCLE_RADS / 12; // Angle measurement in each minute

  if(theHour < 3) {
    theHour += 12;
  }

  theHour -= 3; // Rotate my coordiate so 12 is the new "pole"

  return theHour * hourSlice;
}

void drawInnerCircle() {
  mylcd.Set_Draw_color(INNER_CIRCLE_COLOR);
  mylcd.Fill_Circle(clockPos.x, clockPos.y, INNER_CIRCLE_RADIUS);
  mylcd.Set_Draw_color(CLOCK_HASH_COLOR);
  mylcd.Fill_Circle(clockPos.x, clockPos.y, INNER_CIRCLE_RADIUS / 20);
}

// Expects hour from 0 to 11, with 0 being 12 AM/PM
void drawTimeHash(int theHour) {
  HashPos myPos = clockPos.hash_pos[theHour];

  mylcd.Set_Draw_color(CLOCK_HASH_COLOR);
  
  if(0 == (theHour % 3)) {
    mylcd.Fill_Rectangle(myPos.x1, myPos.y1, myPos.x2, myPos.y2);
  } else {
    mylcd.Draw_Line(myPos.x1, myPos.y1, myPos.x2, myPos.y2);
  }
}

void drawFace() {
  // First draw filled circle to get back color of clock
  mylcd.Set_Draw_color(CLOCK_FACE_COLOR);
  mylcd.Fill_Circle(clockPos.x, clockPos.y, clockPos.r);

  // Now draw outline of clock in CLOCK_HASH_COLOR
  mylcd.Set_Draw_color(CLOCK_HASH_COLOR);
  mylcd.Draw_Circle(clockPos.x, clockPos.y, clockPos.r);  

  // Then draw lines for 12 time marks
  for(int myHour = 0; myHour < 12;myHour++) {
     drawTimeHash(myHour);
  }

  //drawInnerCircle();
}

void eraseLastMinute() {
  mylcd.Set_Draw_color(CLOCK_FACE_COLOR);
  mylcd.Fill_Triangle(lastMinutePos.x1, lastMinutePos.y1,
    lastMinutePos.x2, lastMinutePos.y2, lastMinutePos.x3, lastMinutePos.y3);  
}

void eraseLastHour() {
  mylcd.Set_Draw_color(CLOCK_FACE_COLOR);
  mylcd.Fill_Triangle(lastHourPos.x1, lastHourPos.y1,
    lastHourPos.x2, lastHourPos.y2, lastHourPos.x3, lastHourPos.y3);
}

void drawHour(int theHour, int theMinute, bool firstTime) {
  if(!firstTime) {
    eraseLastHour();
  }
  
  // first determine end of the hour hand triangle 
  // which is farthest point away from center of clock.
  // Use (x3,y3)
  float fractionalHour = (1.0 * theHour) + (theMinute / 60.0); 
  float hourAngle = getAngleForHour(fractionalHour);
  getClockPoint(HOUR_HAND_LENGTH, hourAngle, &lastHourPos.x3, &lastHourPos.y3);

  // Now get 2 base points, each is perpendicular to hour hand
  float firstPhi = hourAngle - (CIRCLE_RADS / 4);
  float secondPhi = hourAngle + (CIRCLE_RADS / 4);
  int baseR = HOUR_HAND_BASE / 2;

  getClockPoint(baseR, firstPhi, &lastHourPos.x1, &lastHourPos.y1);
  getClockPoint(baseR, secondPhi, &lastHourPos.x2, &lastHourPos.y2);

  // Now draw hour hand
  mylcd.Set_Draw_color(HOUR_HAND_COLOR);
  mylcd.Fill_Triangle(lastHourPos.x1, lastHourPos.y1,
    lastHourPos.x2, lastHourPos.y2, lastHourPos.x3, lastHourPos.y3); 
}

void drawMinute(int theMinute, bool firstTime) {
  if(!firstTime) {
    eraseLastMinute();
  }

  // first determine end of the minute hand triangle 
  // which is farthest point away from center of clock.
  // Use (x3,y3)
  float minuteAngle = getAngleForMinute(theMinute);
  getClockPoint(MINUTE_HAND_LENGTH, minuteAngle, &lastMinutePos.x3, &lastMinutePos.y3);

  // Now get 2 base points, each is perpendicular to hour hand
  float firstPhi = minuteAngle - (CIRCLE_RADS / 4);
  float secondPhi = minuteAngle + (CIRCLE_RADS / 4);
  int baseR = MINUTE_HAND_BASE / 2;

  getClockPoint(baseR, firstPhi, &lastMinutePos.x1, &lastMinutePos.y1);
  getClockPoint(baseR, secondPhi, &lastMinutePos.x2, &lastMinutePos.y2);

  // Now draw hour hand
  mylcd.Set_Draw_color(MINUTE_HAND_COLOR);
  mylcd.Fill_Triangle(lastMinutePos.x1, lastMinutePos.y1,
    lastMinutePos.x2, lastMinutePos.y2, lastMinutePos.x3, lastMinutePos.y3); 
  
}

void drawHands(DateTime now, bool firstTime) {
  drawHour(now.hour(), now.minute(), firstTime);
  drawMinute(now.minute(), firstTime);
}


// Draw a 
void drawClock(bool firstTime) {
  if(firstTime) {
    drawFace();
  }

  DateTime now = clock.now();
  
  if(firstTime || (now.minute() != lastMinute)) { 
    drawHands(now, firstTime);
  }

  drawInnerCircle();
  
  if(firstTime || (now.day() != lastDay)) { 
    drawDate(now, firstTime);
  }

  lastMinute = now.minute();
  lastDay = now.day();
}


// Used to draw lines for 1,2,4,5,7,8,10,11
void calculateHashPosForLine(int theHour, int* x1, int* y1, int* x2, int* y2) {

  float phi = getAngleForHour(theHour);

  Serial.println("Hash angle calculated for hour " + String(theHour) +
    " is " + String(phi));
    
  int r = clockPos.r - HASH_LINE_SIZE - 1; // The first point 

  Serial.println("    R for inner point of line is " + String(r));
  
  getClockPoint(r, phi, x1, y1);

  Serial.println("    Inner point of hash is (" + String(*x1) +
    "," + String(*y1) + ")");

  
  r = clockPos.r - 1; // The second point
  getClockPoint(r, phi, x2, y2);
  Serial.println("    Outer point of hash is (" + String(*x1) +
    "," + String(*y1) + ")");
}

// Expects hour from 0 to 11, 0 = 12 AM/PM
void calculateHashPos(HashPos* hash_pos, int theHour) {
  switch(theHour) {
    case 0: // RECTANGLE
      hash_pos->x1 = clockPos.x - (HASH_RECT_WIDTH / 2);
      hash_pos->y1 = clockPos.y - clockPos.r + 1;
      hash_pos->x2 = hash_pos->x1 +  HASH_RECT_WIDTH;
      hash_pos->y2 = hash_pos->y1 + HASH_RECT_HEIGHT;
      break;
    case 3: // RECTANGLE sideways
      hash_pos->x1 = clockPos.x + clockPos.r - HASH_RECT_HEIGHT - 1;
      hash_pos->y1 = clockPos.y - (HASH_RECT_WIDTH / 2);
      hash_pos->x2 = hash_pos->x1 +  HASH_RECT_HEIGHT;
      hash_pos->y2 = hash_pos->y1 + HASH_RECT_WIDTH;
      break;
    case 6: // RECTANGLE
      hash_pos->x1 = clockPos.x - (HASH_RECT_WIDTH / 2);
      hash_pos->y1 = clockPos.y + clockPos.r;
      hash_pos->x2 = hash_pos->x1 +  HASH_RECT_WIDTH;
      hash_pos->y2 = hash_pos->y1 - HASH_RECT_HEIGHT;
      break;
    case 9: // RECTANGLE sideways
      hash_pos->x1 = clockPos.x - clockPos.r + 1;
      hash_pos->y1 = clockPos.y - (HASH_RECT_WIDTH / 2);
      hash_pos->x2 = hash_pos->x1 +  HASH_RECT_HEIGHT;
      hash_pos->y2 = hash_pos->y1 + HASH_RECT_WIDTH;      
      break;
    default: // LINE
    calculateHashPosForLine(
        theHour,
        &hash_pos->x1,
        &hash_pos->y1,
        &hash_pos->x2,
        &hash_pos->y2);
  }
}

void calculateClockPosition() {
  screenWidth = mylcd.Get_Display_Width();
  screenHeight = mylcd.Get_Display_Height();

  // We expect to run in vertical position, where width is less than height

  // The x origin
  clockPos.r = (screenWidth - 2 * CLOCK_PADDING) / 2;
  clockPos.x = CLOCK_PADDING + clockPos.r;
  clockPos.y = CLOCK_PADDING + clockPos.r;

  // Now set the positions for the 12 hashes for hours
  for(int myHour = 0; myHour < 12; myHour++) {
    calculateHashPos(&clockPos.hash_pos[myHour], myHour);
  }
}

void setup() 
{
  Serial.begin(9600);
  if(!clock.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    abort();
  }

  //clock.adjust(DateTime(2020, 12, 30, 14, 34, 0));;
  
  if (! clock.isrunning()) {
    Serial.println("RTC is NOT running, let's set the time!");
    // When time needs to be set on a new device, or after a power loss, the
    // following line sets the RTC to the date & time this sketch was compiled
    clock.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }
    
  mylcd.Init_LCD();
  Serial.println(mylcd.Read_ID(), HEX);

  mylcd.Set_Rotation(0);
  mylcd.Set_Text_Back_colour(BLACK);
  mylcd.Fill_Screen(SCREEN_BACK_COLOR);

  // Now setup Analog Clock Info
  calculateClockPosition();
}

void loop() 
{
  curMillis = millis();

  if(0 == lastMillis ||
      curMillis < lastMillis || // millis rolled over
      (curMillis - lastMillis) >= UPDATE_MILLIS) {
    drawClock(firstRun);
    Serial.println("Time to draw the clock!");
    firstRun = false;
    lastMillis = curMillis;
  }

  
}
