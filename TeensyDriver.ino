/* COLOR ORGAN DRIVER 2.0
 *  Used to Drive the 8 WS2812b LED Strips mounted to the track lighting of the
 *  apartment. Programs to a Teensy 3.2 with an OctoWS2811 adapter board. The
 *  adapter board connects the teensy pins to the two 75ft CAT6 cables that carry the
 *  precisely timed signals to the LED strips. In this implemetation, all color
 *  and effects are computed on the Teensy board, with an FFT result coming over the
 *  serial bus to drive any color organ type effects.
 *
 *
 *  LED Counts in each strip
 *    Strip     LEDS
 *      1       37
 *      2       37
 *      3       37
 *      4       37
 *
 *
 *  For the OctoWS2811 Library to work, all Strips are controlled as having 146 LEDs.
 *  A modification was made to the LATCH pulse in the driver library - it needed to be longer to
 *  counter-act the CAT6 delay and signal degredation over 75ft. The extra pixel data on the 132
 *  pixel strips gets shifted out the end before the Latch signal is sent. The smaller strips use
 *  a mapping function for ease of effects processing
 *
 *  The OctoWS2812b chip uses the following timing model:
 *  HIGH/LOW (1 Bit) = 1.25us
 *  PIXEL (24 Bits) = 30us
 *  LATCH = 200us
 *
 *  Therefore, at 146 pixels per strip, the refresh rate of the LEDs is:
 *  REFRESH_RATE = 218Hz
 *
 *  Effects computations should be efficient enough on the CPU to not slow down this Refresh rate.
 *
 */

/**********************************************
 *
 * Setting up the WS2811 Driving Library
 *
 **********************************************/

#define HWSERIAL Serial1
#define USE_OCTOWS2811
#include<OctoWS2811.h>

/**********************************************
 *
 * Define main LED array, The mapping to the physical Output array is as follows:
 *
 * Strip    LED range     Output Range
 * 1        0-37         0-37
 *
 **********************************************/

const int ledsPerStrip = 37;
int LED[ledsPerStrip*8]; //Main Working LED buffer all effects write to
int LED_out[ledsPerStrip*8]; //LED output buffer all effects write to
int lightness[ledsPerStrip*8]; //Main lightness buffer all effects write to

DMAMEM int displayMemory[ledsPerStrip*8];
int drawingMemory[ledsPerStrip*8];
int rainbowColors[360];

const int config = WS2811_GRB | WS2811_800kHz;

OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config);

const uint8_t PROGMEM gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

/**********************************************
 *
 * Void Setup(): Initialize Serial Port and LED driver
 *
 **********************************************/

void setup() {
  Serial.begin(115200);
  HWSERIAL.begin(38400);
  Serial.setTimeout(50);
  leds.begin();
  leds.show();
  for(int i = 0; i<ledsPerStrip;i++) LED[i]=0; //Initialize LED array
  for (int i=0; i<360; i++) {
    int hue = i;
    int saturation = 100;
    int lightness = 50;
    // pre-compute the 360 rainbow colors
    rainbowColors[i] = makeColor(hue, saturation, lightness);
  }
}

/**********************************************
 *
 * More Global Variables
 *
 **********************************************/

unsigned int incomingByte;
unsigned int color;
unsigned int color2;
unsigned int lastByte;
unsigned int last2Byte;
unsigned int red = 0;
unsigned int green = 0;
unsigned int blue = 0;
unsigned int timingStart = 0;
unsigned int timingInterval = 0;
unsigned int timingStart2 = 0;
unsigned int timingInterval2 = 0;
unsigned int decayStart = 0;
unsigned int decayInterval = 0;
unsigned int rainbowIndex = 0;
unsigned int indexOffset = 0;
unsigned int wait = 0;
unsigned int stripNum = 0;

/**********************************************
 *
 * void MapWrite(): Writes data to the LED Strips. Goes Strip By Strip and Maps the LED array to hardware setup
 *
 * Mapping is as Follows:
 *
 * Strip    LED range     Output Range
 * 1        0-145         0-145
 * 2        556-701       146-291
 * 3        146-277       292-437
 * 4        702-833       438-583
 * 5        278-409       584-729
 * 6        834-965       730-875
 * 7        410-555       876-1021
 * 8        966-1111      1022-1167
 *
 **********************************************/

void MapWrite() {

  for(int i=0; i < ledsPerStrip; i++) {
    //Strip 1
    leds.setPixel(i, LED_out[i]);
    //leds.setPixel(i, 0xFFFFFF);
    //Strip 1
    leds.setPixel(i+ledsPerStrip, LED_out[i]);
    //Strip 1
    leds.setPixel(i+ledsPerStrip*2, LED_out[i]);
    //Strip 1
    leds.setPixel(i+ledsPerStrip*3, LED_out[i]);
  }

//Write to the Ping-Pong DMA
  leds.show();
}

/**********************************************
 *
 * void ColorCorrect(): Apply Gamnma Correction t oo RGB Channels
 *
 **********************************************/

void ColorCorrect() {
  unsigned int red, green, blue;
  for (int i=0;i<=ledsPerStrip*4;i++) {
    red = (pgm_read_byte(&gamma8[(LED[i] >> 16) & 0x0000FF]) << 16) & 0xFF0000;
    green = (pgm_read_byte(&gamma8[(LED[i] >> 8) & 0x0000FF]) << 8) & 0x00FF00;
    blue = pgm_read_byte(&gamma8[LED[i] & 0x0000FF]);
    LED_out[i] = 0;
    LED_out[i] = red | green | blue;
  }
}


/**********************************************
 *
 * void loop(): Main Program Loop
 *
 **********************************************/

unsigned int i = 0;
unsigned int odd = 0;

void loop() {
  // if there's any serial available, read it:

  while (Serial.available() > 0) {
    last2Byte = lastByte;
    lastByte = incomingByte;
    incomingByte = Serial.read();
  }

  //ZeroChangeColor();
 //HueShift(120);

  ConstantRainbow(100);
  //HueShift(100);
  //LightnessShift(100);
  KineticBumpCenter(4);
  //allSame(0);
  //segmentOrgan(50, 5);
  //KineticBigBumpCenter(0);

  //KineticBigBump();
  //KineticFlipFlop();
  //rainbowSparkle(10, 2);


  ColorCorrect();
  MapWrite();
}
/**********************************************
 *
 * void rainbowSparkle():
 * On each cycle, the pixels decrease in lightness by 1
 *
 **********************************************/
void rainbowSparkle(unsigned int timingPeriod, unsigned int decayPeriod){
   timingInterval = millis() - timingStart;
   decayInterval = millis() - decayStart;
   if(timingInterval >= timingPeriod){
       timingStart = millis();
       rainbowIndex++;
       if(rainbowIndex == 360){
         rainbowIndex = 0;
       }
   }
  if(wait==2){
    int pos = random(ledsPerStrip*4);
    lightness[pos] = 75;
    wait=0;
  }
  wait++;
  for(int i=0;i<ledsPerStrip*4;i++){
    if(decayInterval >= decayPeriod){
      decayStart = millis();
      if(lightness[i] > 0){
        lightness[i]--;
      } else {
        LED[i] = 0;
      }
    }
    LED[i]=makeColor(rainbowIndex, 100, lightness[i]);
  }
  MapWrite();
}

/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

void KineticBumpCenter(int wait){
  int bumpWidth = 4;
  //Strip 1 Bump, 146 Pixels
  for(int j=floor(ledsPerStrip/2)-bumpWidth; j<floor(ledsPerStrip/2)+bumpWidth; j++){
    LED[j] = color;
  }
  for(int j=0; j<floor(ledsPerStrip/2)-bumpWidth; j++){
    LED[j] = LED[j+1];
  }
  for(int j=ledsPerStrip; j>=floor(ledsPerStrip/2)+bumpWidth; j--){
    LED[j] = LED[j-1];
  }
    delay(wait);
}
/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

void allSame(int wait){

  for(int j=0; j<ledsPerStrip*4; j++){
    LED[j] = 0xFFFFFF;
  }
  delay(wait);

}

/**********************************************
 *
 * void segmentOrgan(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

 void segmentOrgan(unsigned int timingPeriod2, unsigned int decayInterval){
   timingInterval2 = millis() - timingStart2;
   if(timingInterval2 >= timingPeriod2){
       timingStart2 = millis();
       stripNum++;
       if(stripNum == 8){
         stripNum = 0;
       }
   }
   for(int j=0; j<=1111; j++){
     if(LED[j] > decayInterval) {
      LED[j] = LED[j] - decayInterval;
     } else {
      LED[j] = 0;
     }
   }
   switch(stripNum){
    case 0:
      for(int j=0; j<=145; j++){
        LED[j] = color;
      }
      break;
    case 1:
      for(int j=146; j<=277; j++){
        LED[j] = color;
      }
      break;
    case 2:
      for(int j=278; j<=409; j++){
        LED[j] = color;
      }
      break;
    case 3:
      for(int j=410; j<=555; j++){
        LED[j] = color;
      }
      break;
    case 4:
      for(int j=556; j<=701; j++){
        LED[j] = color;
      }
      break;
    case 5:
      for(int j=702; j<=833; j++){
        LED[j] = color;
      }
      break;
    case 6:
      for(int j=834; j<=965; j++){
        LED[j] = color;
      }
      break;
    case 7:
      for(int j=966; j<=1111; j++){
        LED[j] = color;
      }
      break;
   }
 }

/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

void KineticBigBumpCenter(int wait){
  //Strip 1 Bump, 146 Pixels
  for(int j=53; j<93; j++){
    LED[j] = color;
  }
  for(int j=0; j<53; j++){
    LED[j] = LED[j+1];
  }
  for(int j=145; j>=93; j--){
    LED[j] = LED[j-1];
  }

  //Strip 3 Bump, 132 Pixels
  for(int j=252; j<=277; j++){
    LED[j] = color;
  }
  for(int j=146; j<252; j=j+2){
    LED[j] = LED[j+2];
    LED[j+1] = LED[j+2];
  }
  delay(wait);

}

/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

void KineticBigBump(){

  //Strip 3 Bump, 132 Pixels
  for(int j=0; j<=277; j++){
    LED[j] = color;
  }


}
/**********************************************
 *
 * void zeroChangeColor: The OG. Takes Sound data and produces a color based on whether the signal crossed zero
 *
 **********************************************/

void ZeroChangeColor(){

  if ((incomingByte == 0) && (lastByte == 0) && (last2Byte != 0)) {
    if(i<5){
      i++;
      }
      else
      i=0;
  }
    switch (i) {
      case 0: {
        color = (int(incomingByte << 16) & 0xFF0000);
        break;
      }
      case 1: {
        color = (int(incomingByte/2 << 16) & 0xFF0000);
        color = color | (int(incomingByte/2 << 8) & 0x00FF00);
        break;
      }
      case 2: {
        color = (int(incomingByte << 8) & 0x00FF00);
        break;
      }
      case 3: {
        color = (int(incomingByte/2 << 8) & 0x00FF00);
        color = color | (int(incomingByte/2) & 0x0000FF);
        break;
      }
      case 4: {
        color = int(incomingByte) & (0x0000FF);
        break;
      }
      case 5: {
        color = incomingByte/2 & (0x0000FF);
        color = color | (int(incomingByte/2 << 16) & 0xFF0000);
        break;
      }
    }
}

/**********************************************
 *
 * void ConstantRainbow: The OG. Takes Sound data and produces a rainbow that just cycles through all the time
 *
 **********************************************/

 void ConstantRainbow(unsigned int timingPeriod){
   timingInterval = millis() - timingStart;
   if(timingInterval >= timingPeriod){
       timingStart = millis();
       rainbowIndex++;
       if(rainbowIndex == 360){
         rainbowIndex = 0;
       }
   }
   //Build Color using intensity
   color = 0x000000;
   color = ((rainbowColors[rainbowIndex] & 0x0000FF) * incomingByte)/255;
   color = color | (((((rainbowColors[rainbowIndex] & 0x00FF00) >> 8) * incomingByte)/255) << 8);
   color = color | (((((rainbowColors[rainbowIndex] & 0xFF0000) >> 16) * incomingByte)/255) << 16);
 }

/**********************************************
 *
 * void ConstantRainbow: The OG. Takes Sound data and produces a rainbow that just cycles through all the time
 *
 **********************************************/

 void HueShift(unsigned int timingPeriod){
   timingInterval = millis() - timingStart;
   if(timingInterval >= timingPeriod){
       timingStart = millis();
       rainbowIndex++;
       if(rainbowIndex == 360){
         rainbowIndex = 0;
       }
   }
   indexOffset = (incomingByte/4) + rainbowIndex;
   if(indexOffset >=360){
    indexOffset = indexOffset-360;
   }
   color = rainbowColors[indexOffset];
 }


 void LightnessShift(unsigned int timingPeriod){
   timingInterval = millis() - timingStart;
   if(timingInterval >= timingPeriod){
       timingStart = millis();
       rainbowIndex++;
       if(rainbowIndex == 360){
         rainbowIndex = 0;
       }
   }
   color = makeColor(rainbowIndex, 100, 10+(incomingByte/4));
 }

/*
 *
 * COLOR TIME
 *
 *
 */
// Convert HSL (Hue, Saturation, Lightness) to RGB (Red, Green, Blue)
//
//   hue:        0 to 359 - position on the color wheel, 0=red, 60=orange,
//                            120=yellow, 180=green, 240=blue, 300=violet
//
//   saturation: 0 to 100 - how bright or dull the color, 100=full, 0=gray
//
//   lightness:  0 to 100 - how light the color is, 100=white, 50=color, 0=black
//
int makeColor(unsigned int hue, unsigned int saturation, unsigned int lightness)
{
  unsigned int red, green, blue;
  unsigned int var1, var2;

  if (hue > 359) hue = hue % 360;
  if (saturation > 100) saturation = 100;
  if (lightness > 100) lightness = 100;

  // algorithm from: http://www.easyrgb.com/index.php?X=MATH&H=19#text19
  if (saturation == 0) {
    red = green = blue = lightness * 255 / 100;
  } else {
    if (lightness < 50) {
      var2 = lightness * (100 + saturation);
    } else {
      var2 = ((lightness + saturation) * 100) - (saturation * lightness);
    }
    var1 = lightness * 200 - var2;
    red = h2rgb(var1, var2, (hue < 240) ? hue + 120 : hue - 240) * 255 / 600000;
    green = h2rgb(var1, var2, hue) * 255 / 600000;
    blue = h2rgb(var1, var2, (hue >= 120) ? hue - 120 : hue + 240) * 255 / 600000;
  }
  return (red << 16) | (green << 8) | blue;
}

unsigned int h2rgb(unsigned int v1, unsigned int v2, unsigned int hue)
{
  if (hue < 60) return v1 * 60 + (v2 - v1) * hue;
  if (hue < 180) return v2 * 60;
  if (hue < 240) return v1 * 60 + (v2 - v1) * (240 - hue);
  return v1 * 60;
}

/**********************************************
 *
 * NON COLOR ORGAN EFFECTS
 *
 *
 *
 *
 *
 *
 * For Funsies
 *
 *
 *
 *
 *
 **********************************************/



/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/

void Aurora(){


}

/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/
 void KineticFlipFlop(){
  if ((incomingByte == 0) && (lastByte == 0) && (last2Byte != 0)) {
    if(odd==0){
      odd=1;
      }
      else
      odd=0;
  }

if(i==0) {
  //Strip 1 Bump, 146 Pixels
  for(int j=53; j<93; j++){
    LED[j] = color;
  }

  //Strip 3 Bump, 132 Pixels
  for(int j=252; j<=277; j++){
    LED[j] = color;
  }

} else {

  //Strip 7 Bump, 146 Pixels
  for(int j=463; j<503; j++){
    LED[j] = color;
  }

  //Strip 5 Bump, 132 Pixels
  for(int j=384; j<=409; j++){
    LED[j] = color;
  }


}
/*
//Even moves
  for(int j=0; j<53; j++){
    LED[j] = LED[j+1];
  }
  for(int j=145; j>=93; j--){
    LED[j] = LED[j-1];
  }
  for(int j=146; j<252; j=j+2){
    LED[j] = LED[j+2];
    LED[j+1] = LED[j+2];
  }
 */

}

/**********************************************
 *
 * void KineticBumpCenter(): The OG. Takes Sound data and produces a bump in the center of each strip segment.
 * On each cycle, the pixel data shifts 1 pixel out towards the end of the strip segment.
 *
 **********************************************/
