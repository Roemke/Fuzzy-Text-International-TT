#include <pebble.h>

#include "num2words.h"

#define DEBUG 0
//if debug to 1 appinfo.json.watchapp has to be used - not sure, but nearly :-)
//so copy it 
#define MAX_WIDTH_PX 140  //kr  

//for function calls when updating lines see readmeKR.txt
//maybe I need to change the original flow because of the font
//problem 

#define NUM_LINES 4
#define LINE_LENGTH 7 //
#define BUFFER_SIZE (LINE_LENGTH + 10) //for smaller font we need more characters
#define ROW_HEIGHT 37 //was 37
#define ROW_HEIGHT_SMALL 30 //kr

//define keys for persistence on pebble, need match in pebble-js-app
#define TIMETABLE_KEY 0 //how many entries in timetable
#define TEXT_ALIGN_KEY 1
#define LANGUAGE_KEY 2
#define DELTA_KEY 3   
#define DONE_KEY 4  //show minutes done   
#define BATTERY_KEY 5 // battery bar 
#define WARNOWN_KEY 6 // warning / vibration 5 minutes before own lessen 
#define HOURS_KEY 7 // hours in custom language
#define RELS_KEY 8 // rels in custom language
#define INVERT_KEY 10 
#define BATTERY_PHONE_KEY 11
#define SHAKE_KEY 12 //if shake your wrist datamode is enabled

#define BATTERY_LEVEL_KEY 20 //
//-----------------
//keys which are get on JavaScript side, should be only one
#define SEND_BATTERY_KEY 1 



//---------------------
#define TEXT_ALIGN_CENTER 0
#define TEXT_ALIGN_LEFT 1
#define TEXT_ALIGN_RIGHT 2

// The time it takes for a layer to slide in or out.
#define ANIMATION_DURATION 400
// Delay between the layers animations, from top to bottom
#define ANIMATION_STAGGER_TIME 150
// Delay from the start of the current layer going out until the next layer slides in
#define ANIMATION_OUT_IN_DELAY 100

#define LINE_APPEND_MARGIN 0
// We can add a new word to a line if there are at least this many characters free after
#define LINE_APPEND_LIMIT (LINE_LENGTH - LINE_APPEND_MARGIN)


static int text_align = TEXT_ALIGN_CENTER;
static bool invert = false;
static bool delta = false; //kr options static, oh c kann bool dachte waere c++
	                        //delta is for showing exact - fuzzy
static bool done = false; //show minutes done and left
static bool battery = true; //show bar to indicate battery
static int batteryLevel = -1;
static bool batteryPhone = false;
static int  batteryPhoneLevel = -1; //level of phone
static bool shakeDetect = false; //detect shaking of wrist to enable datamode
static bool warnown = false;
static int tt_entries = 0; //how many entries in the TimeTable

static bool dataMode = false; //false; //show date time and additional info instead of fuzzy-time

char * customHours[24]={0}; //if we have custom hours, use this 
char * customRels[12]={0}; //should be complete 0 
 
static Language lang = EN_US;

static Window *window;

typedef struct {
	TextLayer *currentLayer;
	TextLayer *nextLayer;
	char lineStr1[BUFFER_SIZE];
	char lineStr2[BUFFER_SIZE];
	PropertyAnimation *animation1;
	PropertyAnimation *animation2;
	unsigned int currentFontSmall;
	unsigned int nextFontSmall;
} Line;

typedef struct TTEntry 
{
  unsigned int startMin;
  unsigned int endMin;
  unsigned int position; //wo muesste der eintrag stehen
  bool own;
  struct TTEntry * next;
} TTEntry;

static TTEntry * ttPerDay[]={0,0,0,0,0,0,0}; //7 Listen fuer die Tage

static Line lines[NUM_LINES];
#ifdef PBL_BW 
static InverterLayer *inverter_layer;
#endif
static TextLayer * tempLayer ; // = text_layer_create(GRect(144, 0, 144, 50));
char tempLayerText[BUFFER_SIZE];

static TextLayer * deltaLayer; 
char deltaLayerText[]="(+0)"; // ( sign digit ) \0 without spaces 

static TextLayer * doneLayerL; 
static TextLayer * doneLayerR; 
static char doneLayerTextL[12]=""; 
static char doneLayerTextR[12]=""; 

static Layer * batteryLayer;

//static time was *t ,  rename it
static struct tm *actual_time; //renamed from t in the original source
#if DEBUG
static  struct tm debug_time = {.tm_sec=0, .tm_min=0, .tm_hour=0};
#endif
static int currentNLines;


//error messages 
char *translate_error(AppMessageResult result) {
  switch (result) {
    case APP_MSG_OK: return "APP_MSG_OK";
    case APP_MSG_SEND_TIMEOUT: return "APP_MSG_SEND_TIMEOUT";
    case APP_MSG_SEND_REJECTED: return "APP_MSG_SEND_REJECTED";
    case APP_MSG_NOT_CONNECTED: return "APP_MSG_NOT_CONNECTED";
    case APP_MSG_APP_NOT_RUNNING: return "APP_MSG_APP_NOT_RUNNING";
    case APP_MSG_INVALID_ARGS: return "APP_MSG_INVALID_ARGS";
    case APP_MSG_BUSY: return "APP_MSG_BUSY";
    case APP_MSG_BUFFER_OVERFLOW: return "APP_MSG_BUFFER_OVERFLOW";
    case APP_MSG_ALREADY_RELEASED: return "APP_MSG_ALREADY_RELEASED";
    case APP_MSG_CALLBACK_ALREADY_REGISTERED: return "APP_MSG_CALLBACK_ALREADY_REGISTERED";
    case APP_MSG_CALLBACK_NOT_REGISTERED: return "APP_MSG_CALLBACK_NOT_REGISTERED";
    case APP_MSG_OUT_OF_MEMORY: return "APP_MSG_OUT_OF_MEMORY";
    case APP_MSG_CLOSED: return "APP_MSG_CLOSED";
    case APP_MSG_INTERNAL_ERROR: return "APP_MSG_INTERNAL_ERROR";
    default: return "UNKNOWN ERROR";
  }
}
// Animation handler
static void animationStoppedHandler(struct Animation *animation, bool finished, void *context)
{
	TextLayer *current = (TextLayer *)context;
	GRect rect = layer_get_frame((Layer *)current);
	rect.origin.x = 144;
	layer_set_frame((Layer *)current, rect);
}

// Animate line
static void makeAnimationsForLayer(Line *line, int delay)
{
	TextLayer *current = line->currentLayer;
	TextLayer *next = line->nextLayer;

	// Destroy old animations
	//no need if we are on a color-pebble
	#ifdef PBL_BW
	if (line->animation1 != NULL)
	{
		 property_animation_destroy(line->animation1);
	}
	if (line->animation2 != NULL)
	{
		 property_animation_destroy(line->animation2);
	}
  #endif
	// Configure animation for current layer to move out
	GRect rect = layer_get_frame((Layer *)current);
	rect.origin.x =  -144;
	line->animation1 = property_animation_create_layer_frame((Layer *)current, NULL, &rect);
	animation_set_duration(property_animation_get_animation(line->animation1), ANIMATION_DURATION);
	animation_set_delay(property_animation_get_animation(line->animation1), delay);
	animation_set_curve(property_animation_get_animation(line->animation1), AnimationCurveEaseIn); // Accelerate

	// Configure animation for current layer to move in
	GRect rect2 = layer_get_frame((Layer *)next);
	rect2.origin.x = 0; //looks correct but string with width 142 does not fit into layer
	line->animation2 = property_animation_create_layer_frame((Layer *)next, NULL, &rect2);
	animation_set_duration(property_animation_get_animation(line->animation2), ANIMATION_DURATION);
	animation_set_delay(property_animation_get_animation(line->animation2), delay + ANIMATION_OUT_IN_DELAY);
	animation_set_curve(property_animation_get_animation(line->animation2), AnimationCurveEaseOut); // Deaccelerate

	// Set a handler to rearrange layers after animation is finished
	animation_set_handlers(property_animation_get_animation(line->animation2), (AnimationHandlers) {
		.stopped = (AnimationStoppedHandler)animationStoppedHandler
	}, current);

	// Start the animations
	animation_schedule(property_animation_get_animation(line->animation1));
	animation_schedule(property_animation_get_animation(line->animation2));	
}

static void updateLayerText(TextLayer* layer, char* text)
{
	const char* layerText = text_layer_get_text(layer);
	strcpy((char*)layerText, text);
	//(kr) hier haben wir die breite / koennen sie auslesen
	// To mark layer dirty
	text_layer_set_text(layer, layerText);
    //layer_mark_dirty(&layer->layer); //kr war auskommentiert - unnoetig?
}

// Update line
static void updateLineTo(Line *line, char *value, int delay)
{
	updateLayerText(line->nextLayer, value);
	makeAnimationsForLayer(line, delay);

	// Swap current/next layers
	TextLayer *tmp = line->nextLayer;
	line->nextLayer = line->currentLayer;
	line->currentLayer = tmp;
	line->currentFontSmall = line->nextFontSmall;
}

// Check to see if the current line needs to be updated
static bool needToUpdateLine(Line *line, char *nextValue)
{
	const char *currentStr = text_layer_get_text(line->currentLayer);

	if (strcmp(currentStr, nextValue) != 0 ||
	    line->currentFontSmall != line->nextFontSmall) {
		return true;
	}
	return false;
}

static GTextAlignment lookup_text_alignment(int align_key)
{
	GTextAlignment alignment;
	switch (align_key)
	{
		case TEXT_ALIGN_LEFT:
			alignment = GTextAlignmentLeft;
			break;
		case TEXT_ALIGN_RIGHT:
			alignment = GTextAlignmentRight;
			break;
		default:
			alignment = GTextAlignmentCenter;
			break;
	}
	return alignment;
}

// 2015-02-12 (kr) configure layer 
static void configureLayer(TextLayer *textlayer,const char * fontKey )
{
	text_layer_set_font(textlayer, fonts_get_system_font(fontKey));
	text_layer_set_text_color(textlayer, GColorWhite);
	text_layer_set_background_color(textlayer, GColorClear);
	text_layer_set_text_alignment(textlayer, lookup_text_alignment(text_align));
}

// Configure the layers for the given text
//returns number of lines we need
/* special cases
  font is two wide 
  we have 4 lines but need room at the top
*/
static int configureLayersForText(char text[NUM_LINES][BUFFER_SIZE], char format[])
{
	int numLines = 0;
        //lines is array of size NUM_LINES (4) of type Line 
	// Set bold layer.
	int i;
	int flagWidth = 0;
  //for layer check
  //TextLayer * tempLayer  = text_layer_create(GRect(144, 0, 144, 50)); besser in window_load evtl. performance


	for (i = 0; i < NUM_LINES; i++) { //normal case
		if (strlen(text[i]) > 0) {
			if (format[i] == 'b')
			{
				configureLayer(lines[i].nextLayer,FONT_KEY_BITHAM_42_BOLD); //Bold for Hours (see in strings... marked with *
			}
			else
			{
				configureLayer(lines[i].nextLayer,FONT_KEY_BITHAM_42_LIGHT);
				configureLayer(tempLayer,FONT_KEY_BITHAM_42_LIGHT);
			}
			lines[i].nextFontSmall = 0; //normal
			//2015-02-12 (kr) try to check size - sieht gut aus, aber es gibt zu wenig fonts
			//nein, hatte oben getestet, hier werde ich die breite noch nicht haben
			//stimmt, hab sie nicht - schade, evtl einen text_layer nur zum checken anlegen
			const char* layerText = text_layer_get_text(tempLayer);
			strcpy((char*)layerText, text[i]);//text_layer_set_tet not necessary
			//if this layer is marked diry is equal cause it's not visible 
			
      //--------------------
			GSize size = text_layer_get_content_size(tempLayer);
			if (format[i] != 'b' && size.w > MAX_WIDTH_PX) //need to fix the font and the line arrangement
			{	 //bold lines could be longer (eg sieben in german)
			   flagWidth = 1; //need to rearrange	
			   break;
			}			
			//-----------------			
		}
		else
		{
			break;
		}	
	}
	int yPositions[]={10,47,84,121};
	if (flagWidth ) // problems with width of font - to wide	
	{ //one try with smaller font could be later extende to do while and font list, no, limited fonts 
		for (i = 0; i < NUM_LINES; i++) //looks ok
		{
			if (strlen(text[i]) > 0) {
				if (format[i] == 'b')
				{
					configureLayer(lines[i].nextLayer,FONT_KEY_BITHAM_42_BOLD); //Bold for Hours (see in strings... marked with *
				}
				else
				{
					configureLayer(lines[i].nextLayer,FONT_KEY_BITHAM_30_BLACK);
					lines[i].nextFontSmall = 1;
				}
			}
			else
			{
				break;
			}	
		}		
	}
	// Calculate y position of top Line
	numLines = i;
	if (numLines == 4 && (delta || done)) //top-lines smaller, we need some room at the top
	{
		yPositions[0] = 36;
		yPositions[1] = 60;
		configureLayer(lines[0].nextLayer,FONT_KEY_GOTHIC_28);
		configureLayer(lines[1].nextLayer,FONT_KEY_GOTHIC_28);
	}
	if (dataMode) //we have 3 lines with percent phone percent pebble battery, date, time 
	{ //3 lines 
	  numLines = 3;
	  
	  yPositions[1] = 40;
	  yPositions[2] = 75;
	  yPositions[3] = 110;
	  configureLayer(lines[0].nextLayer,FONT_KEY_GOTHIC_28_BOLD);
	  configureLayer(lines[1].nextLayer,FONT_KEY_GOTHIC_28_BOLD);
	  configureLayer(lines[2].nextLayer,FONT_KEY_ROBOTO_BOLD_SUBSET_49);//_BITHAM_42_BOLD);
	}

	// Set y positions for the lines
	for (int i = 0; i < numLines; i++)
	{
    int ypos = yPositions[i + NUM_LINES - numLines];	  
		layer_set_frame((Layer *)lines[i].nextLayer, GRect(144, ypos, 144, 50)); //x y w h
	}

	return numLines;
}

static int time_to_lines(int hours, int minutes, int seconds, char lines[NUM_LINES][BUFFER_SIZE], char format[])
{
	int length = NUM_LINES * BUFFER_SIZE + 1;
	char timeStr[length];
	int deltaVal = time_to_words(lang, hours, minutes, seconds, timeStr, length);
	//(kr) 2015-02-12: positve delta: real time is time + n Minutes
	// Empty all lines
	for (int i = 0; i < NUM_LINES; i++)
	{
		lines[i][0] = '\0';
	}

	char *start = timeStr;
	char *end = strstr(start, " ");
	int l = 0;
	while (end != NULL && l < NUM_LINES) {
		// Check word for bold prefix
		if (*start == '*' && end - start > 1)
		{
			// Mark line bold and move start to the first character of the word
			format[l] = 'b';
			start++;
		}
		else
		{
			// Mark line normal
			format[l] = ' ';
		}

		// Can we add another word to the line?
		if (format[l] == ' ' && *(end + 1) != '*'    // are both lines formatted normal?
			&& end - start < LINE_APPEND_LIMIT - 1)  // is the first word is short enough?
		{
			// See if next word fits
			char *try = strstr(end + 1, " ");
			if (try != NULL && try - start <= LINE_APPEND_LIMIT)
			{
				end = try;
			}
		}

		// copy to line
		*end = '\0';
		strcpy(lines[l++], start);

		// Look for next word
		start = end + 1;
		end = strstr(start, " ");
	}
	return deltaVal;
}

//data-mode: numbers 
static void time_to_lines_DataMode(struct tm * t, char lines[NUM_LINES][BUFFER_SIZE])
{
	for (int i = 0; i < NUM_LINES; i++)
	{
		lines[i][0] = '\0';
	}
	
	if (batteryPhoneLevel != -1 && batteryLevel != -1)
  	snprintf(lines[0],12,"%d%% / %d%%",batteryPhoneLevel, batteryLevel);
  else if (batteryPhoneLevel == -1 && batteryLevel != -1)
  	snprintf(lines[0],12,"-- / %d%%", batteryLevel);
  else if (batteryPhoneLevel != -1 && batteryLevel == -1)
  	snprintf(lines[0],12,"%d%% / --", batteryPhoneLevel);
  else  
  	snprintf(lines[0],12,"-- / --"); //should not/rarely occur
	
	snprintf(lines[1],11,"%02d.%02d.%4d",t->tm_mday, t->tm_mon+1, t->tm_year + 1900);
	snprintf(lines[2],6,"%02d:%02d",t->tm_hour, t->tm_min);
}


//if delta is own we schould show the difference from exact time
void handleDeltaToExact(bool delta, int deltaVal)
{
	if (delta )
	{
		if (dataMode)
		  snprintf(deltaLayerText,5," ");
		else if (deltaVal==0)
			snprintf(deltaLayerText,5," * ");
		else if (deltaVal < 0)
			snprintf(deltaLayerText,5,"(%i)",deltaVal);
		else
			snprintf(deltaLayerText,5,"(+%i)",deltaVal);
		layer_set_hidden((Layer *) deltaLayer,false);
		//layer_set_frame((Layer *) deltaLayer,GRect(xDelta,yDelta,43,28));//x y w h	
	}
	else // frame ausblenden
	{
		layer_set_hidden((Layer *) deltaLayer,true);
		//layer_mark_dirty((Layer *) deltaLayer);
	}	
  layer_mark_dirty((Layer *) deltaLayer); //sicher gehen
}


void   handleIntervalSettings(struct tm *t, bool done, bool warnown)
{

  if (warnown || done) //otherwise nothing to do 
  {
    //search for matching interval
    unsigned int minutes = t->tm_hour * 60 + t->tm_min;
    int tmMyDow = (t->tm_wday + 6 ) % 7; 
    TTEntry * akt = ttPerDay[tmMyDow];
    TTEntry * prev = akt;                           
    while (akt && akt->startMin < minutes)
    {
      prev = akt;
      akt= akt->next;
    }
    /*APP_LOG(APP_LOG_LEVEL_INFO,"Done is %i warnown is %i minutes is %i ",done ? 1 : 0, 
                         warnown ? 1 : 0, minutes);
    if (prev)
      APP_LOG(APP_LOG_LEVEL_INFO,"have prev with start %i", prev->startMin );
    if (akt)
      APP_LOG(APP_LOG_LEVEL_INFO,"have akt with start %i", akt->startMin );
    */                      
    
    if (done)
    {
      layer_set_hidden((Layer *) doneLayerL,false);
      layer_set_hidden((Layer *) doneLayerR,false);
      if ( prev && prev->startMin <= minutes && minutes <= prev->endMin)
      { //bin im Intervall
        snprintf(doneLayerTextL,12,"%i",minutes - prev->startMin);
        snprintf(doneLayerTextR,12,"%i", prev->endMin - minutes );
      }
			else  //outside interval show time to next lesson
			{
        layer_set_hidden((Layer *) doneLayerR,true); //hidden
        //if we show time left, move the delta layer to right side  
        text_layer_set_text_alignment(deltaLayer,GTextAlignmentRight);           
        
			  int minutesLeft=0;
			  int fullDays = 0;
			  if (akt)
			  {                  //have interval on same day
			    minutesLeft = akt->startMin - minutes; 
			  }
			  else                     //next interval on another day
			  {
			    int counter = 0;
			    do
			    {
			      akt = ttPerDay[( tmMyDow + ++counter) % 7];//einen weiter 
			    }while(! akt && counter < 7 );  //fr sa so mo di mi do fr 
			                                    //   1  2  3  4  5  6  7
			                                    //4  5  6  0  1  2  3  4
			    fullDays = counter -1; 
          if (akt) //points to first entry on day
          {
             minutesLeft += 1440 - minutes; //on this day
             minutesLeft += akt->startMin;  //on day with interval
          }
			  }// eo int on another day
        if (fullDays || minutesLeft)
        {
          int hours = minutesLeft / 60;
          int daysFromH = 0;
          if (hours > 23)
          {
            daysFromH = hours / 24;
            hours = hours - daysFromH * 24;
            fullDays += daysFromH; 
          }   
          minutesLeft -= ( 60 * hours + daysFromH * 1440) ;
          doneLayerTextL[0] = '\0';
          if (fullDays)
            snprintf(doneLayerTextL,12,"%id",fullDays);
          if (hours > 0 || fullDays)
            snprintf(doneLayerTextL+strlen(doneLayerTextL), 12-strlen( doneLayerTextL),
                    "%ih",hours);
          snprintf(doneLayerTextL+strlen(doneLayerTextL), 12-strlen( doneLayerTextL),
                    "%i",minutesLeft);             
        }                       
			}// ouside interval, show time left until next lesson starts
    } //end of done
    else
    {
      layer_set_hidden((Layer *) doneLayerL,true);
      layer_set_hidden((Layer *) doneLayerR,true);
    }
    layer_mark_dirty((Layer *) doneLayerL); 
    layer_mark_dirty((Layer *) doneLayerR); 
  
    if (akt  && akt->startMin - minutes == 5 && warnown && akt->own ) //5 Minuten vorher (konfigurierbar machen?)
    {
       //vibrieren
       static const uint32_t const segments[] = {100, 100, 300 };
       VibePattern pat = {
         .durations = segments,
           .num_segments = ARRAY_LENGTH(segments),
       };
       vibes_enqueue_custom_pattern ( pat);
    }
  } 
}

// Update screen based on new time
static void display_time(struct tm *t)
{
	// The current time text will be stored in the following strings
	char textLine[NUM_LINES][BUFFER_SIZE];
	char format[NUM_LINES];

	//(kr) delta is realTime - roundedTime (rounded: 5 minutes interval)
	int deltaVal = 0;
	if (!dataMode)
	  deltaVal = time_to_lines(t->tm_hour, t->tm_min, t->tm_sec, textLine, format);
  else
    time_to_lines_DataMode(t, textLine);
  
  text_layer_set_text_alignment (deltaLayer, GTextAlignmentCenter);
  
  //handle interval specific things, maybe changes alignenment of deltaLayer to right
  if (tt_entries > 0) 
    handleIntervalSettings(t,  done,  warnown);
  
  //if delta we should show delta to exact time
  handleDeltaToExact(delta,deltaVal);
  
	//(kr) hier wird übergeben, aber Text nicht gesetzt
	int nextNLines = configureLayersForText(textLine, format);

	int delay = 0;
	for (int i = 0; i < NUM_LINES; i++) {
		if (nextNLines != currentNLines || needToUpdateLine(&lines[i], textLine[i])) {
			updateLineTo(&lines[i], textLine[i], delay);
			delay += ANIMATION_STAGGER_TIME;
		}
	}
	
	currentNLines = nextNLines; //currentNLines ist static
}

static void initLineForStart(Line* line)
{
	// Switch current and next layer
	TextLayer* tmp  = line->currentLayer;
	line->currentLayer = line->nextLayer;
	line->nextLayer = tmp;

	// Move current layer to screen;
	GRect rect = layer_get_frame((Layer *)line->currentLayer);
	rect.origin.x = 0;
	layer_set_frame((Layer *)line->currentLayer, rect);
}

// Update screen without animation first time we start the watchface
static void display_actual_time(struct tm *t)
{
	// The current time text will be stored in the following strings
  //APP_LOG(APP_LOG_LEVEL_INFO, "call to display_actual_time with %p",t);

	char textLine[NUM_LINES][BUFFER_SIZE];
	char format[NUM_LINES];

	time_to_lines(t->tm_hour, t->tm_min, t->tm_sec, textLine, format);

	// This configures the nextLayer for each line
	currentNLines = configureLayersForText(textLine, format);

	// Set the text and configure layers to the start position
	for (int i = 0; i < currentNLines; i++)
	{
		updateLayerText(lines[i].nextLayer, textLine[i]);
		// This call switches current- and nextLayer
		initLineForStart(&lines[i]);
	}	
}

//send message to phone app to get battery status of phone
static void update_phone_battery_status()
{
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  if (iter == NULL) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "null iter, can't send");
    batteryPhoneLevel = -1;
  }
  else
	{
  	Tuplet tuple =  TupletInteger(SEND_BATTERY_KEY, 1); 
    dict_write_tuplet(iter, &tuple);
    dict_write_end(iter);
    app_message_outbox_send();
  }
}

// Time handler called every minute by the system
static void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  //#if DEBUG==0 //switch off when using debug
	actual_time = tick_time;
	display_time(actual_time);	
	
	bool redrawBattery = false;
	
	if ( batteryPhone && (tick_time->tm_min % 30 == 0 || batteryPhoneLevel == -1 )) //every thirty minutes  
  {
  	update_phone_battery_status();	
  	redrawBattery = true;
	}
	// will need companion app or webservice on phone, I'll try the web service
	
  if ((battery || dataMode) && (tick_time->tm_min % 10 == 0 || batteryLevel == -1 )) //not to expensive I think, do it more often
  {  
    BatteryChargeState charge_state = battery_state_service_peek();
    batteryLevel =  charge_state.charge_percent;
    redrawBattery = battery ; //only draw if battery is set
  }
  if (redrawBattery)
    layer_mark_dirty(batteryLayer);
	//#endif
}

/**
 * Debug methods. For quickly debugging enable debug macro on top to transform the watchface into
 * a standard app and you will be able to change the time with the up and down buttons
 */
#if DEBUG
void initializeDebugTime()
{	
	if ( debug_time.tm_sec == 0 && debug_time.tm_min==0 && debug_time.tm_hour==0) //set time from actual_time
	{
		debug_time.tm_sec = actual_time->tm_sec ;
		debug_time.tm_min = actual_time->tm_min ;
		debug_time.tm_hour = actual_time->tm_hour ;

     debug_time.tm_mday = actual_time->tm_mday;   /* day of the month (1 to 31) */
     debug_time.tm_mon = actual_time->tm_mon;    /* months since January (0 to 11) */
     debug_time.tm_year = actual_time->tm_year;   /* years since 1900 */
     debug_time.tm_wday = actual_time->tm_wday;   /* days since Sunday (0 to 6 Sunday=0) */
     debug_time.tm_yday = actual_time->tm_yday;   /* days since January 1 (0 to 365) */
	}
}
static void up_single_click_handler(ClickRecognizerRef recognizer, void * context){ // Window *window) {
	//(void)recognizer;
	//(void)window; //hmm, it does not work with actual_time (was t in original)
  initializeDebugTime();
	debug_time.tm_sec=0;
	debug_time.tm_min += 5;
	if (debug_time.tm_min >= 60) {
		debug_time.tm_min = 0;
		debug_time.tm_hour += 1;
		
		if (debug_time.tm_hour >= 24) {
			debug_time.tm_hour = 0;
		}
	}
	APP_LOG(APP_LOG_LEVEL_DEBUG,"up click with minutes %i",debug_time.tm_min);
	display_time(&debug_time);
}


static void down_single_click_handler(ClickRecognizerRef recognizer, void * context) {//Window *window) {
	//(void)recognizer;
	//(void)window;
	initializeDebugTime();
	debug_time.tm_sec = 0;
	debug_time.tm_min -= 5;
	if (debug_time.tm_min < 0) {
		debug_time.tm_min = 59;
		debug_time.tm_hour -= 1;
		
		if (debug_time.tm_hour < 0) {
			debug_time.tm_hour = 23;
		}
	}
  APP_LOG(APP_LOG_LEVEL_DEBUG,"down click with minutes %i",debug_time.tm_min);
	display_time(&debug_time);
}

static void up_long_click_handler(ClickRecognizerRef recognizer, void * context) 
{
	initializeDebugTime();
	debug_time.tm_sec = 0;
  debug_time.tm_hour ++;
  if (debug_time.tm_hour >23 ) {
			debug_time.tm_hour = 0;
			debug_time.tm_wday = ( debug_time.tm_wday + 1) % 7;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG,"up long click with hour %i",debug_time.tm_hour);
	display_time(&debug_time);

}

static void down_long_click_handler(ClickRecognizerRef recognizer, void * context) 
{
	initializeDebugTime();
	debug_time.tm_sec = 0;
  debug_time.tm_hour --;
  if (debug_time.tm_hour < 0 ) {
			debug_time.tm_hour = 23;
			debug_time.tm_wday = ( debug_time.tm_wday - 1);
			if (debug_time.tm_wday < 0 )
			  debug_time.tm_wday = 6;
  }
  APP_LOG(APP_LOG_LEVEL_DEBUG,"down long click with hour %i",debug_time.tm_hour);
	display_time(&debug_time);
  
}

static void click_config_provider(void * config){ //ClickConfig **config, Window *window) {
  //(void)window; (kr) ClickConfig unknown, why (void)

  //config[BUTTON_ID_UP]->click.handler = (ClickHandler) up_single_click_handler;
  //config[BUTTON_ID_UP]->click.repeat_interval_ms = 100;
  //config[BUTTON_ID_DOWN]->click.handler = (ClickHandler) down_single_click_handler;
  //config[BUTTON_ID_DOWN]->click.repeat_interval_ms = 100;

  //(kr) ClickHandler seems to be necessary
  window_single_click_subscribe(BUTTON_ID_UP, (ClickHandler) up_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, (ClickHandler) down_single_click_handler);
  window_long_click_subscribe(BUTTON_ID_UP, 0 , (ClickHandler) up_long_click_handler,0);
  window_long_click_subscribe(BUTTON_ID_DOWN, 0 , (ClickHandler) down_long_click_handler,0);
  //delay of 0 means 500 ms
}

#endif

static void init_line(Line* line)
{
	// Create layers with dummy position to the right of the screen
	line->currentLayer = text_layer_create(GRect(144, 0, 144, 50));//x y w h 
	line->nextLayer = text_layer_create(GRect(144, 0, 144, 50));
  //144 means at the right end of the frame?
  
	// Configure a style (kr) reduced functions
	//configureLightLayer(line->currentLayer);
	//configureLightLayer(line->nextLayer);
	configureLayer(line->currentLayer,FONT_KEY_BITHAM_42_LIGHT);	
	configureLayer(line->nextLayer,FONT_KEY_BITHAM_42_LIGHT);
	// Set the text buffers
	line->lineStr1[0] = '\0';
	line->lineStr2[0] = '\0';
	text_layer_set_text(line->currentLayer, line->lineStr1);
	text_layer_set_text(line->nextLayer, line->lineStr2);

	// Initially there are no animations
	line->animation1 = NULL;
	line->animation2 = NULL;
	line->currentFontSmall = 0;
	line->nextFontSmall = 0;
}

static void destroy_line(Line* line)
{
	// Free layers
	text_layer_destroy(line->currentLayer);
	text_layer_destroy(line->nextLayer);
}

static TextLayer * prepareTextLayer (Layer * window_layer, 
     char *layerText, GTextAlignment alignment, int x, int y, int w , int h)
{
  TextLayer * layer=text_layer_create(GRect(x,y,w,h)); 
	text_layer_set_text(layer,layerText); 
  configureLayer(layer,FONT_KEY_GOTHIC_28  );
  text_layer_set_text_alignment(layer,alignment);
	layer_set_hidden((Layer *) layer,false);
	layer_add_child(window_layer, (Layer *)layer);
	return layer;
}

//hmm, called very often, think caused by animation
//but doesnt matter, hmm it matters if asking battery will drain battery
//I think I read something like this concerning the phones battery but on
//pebble I found nothing, hhmm not really sure when it's called, I 
//think I have an overlap, but I need to trigger this in handle_minute
static void batteryDisplayUpdate(Layer * layer, GContext * ctx)
{
  //APP _LOG(APP_LOG_LEVEL_DEBUG,"call to batteryDisplay");
	int ypos = 5;
	if (delta || done) //need some place for text
 		ypos = 28;
	if (battery && batteryLevel != -1)
	{
		int width = batteryLevel /100.0 * 144;		
		graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, ypos, width, 1), 0, GCornerNone);
  }
  
  if (batteryPhone && batteryPhoneLevel != -1)
  {
    int width = batteryPhoneLevel/100.0 * 144;
		graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_rect(ctx, GRect(0, ypos+3, width, 1), 0, GCornerNone);
  }      
}
static void batteryDisplay()
{
	if (battery || batteryPhone)
	{
		layer_set_hidden(batteryLayer,false);
		layer_mark_dirty(batteryLayer);    
	}
	else
		layer_set_hidden(batteryLayer,true);    
}


/* hmm, strchr exists is not documented 
static char * mystrchr (char * hs, char n)
{
  char * res = 0;
  while (*hs)
  {
    if (*hs == n)
    {
      res = hs;
      break;
    }
    hs++;
  }
  return res;
}
*/

static int getTimeData(char * tupleString, int *sh, int *sm, int *eh, int *em, bool *own)
{
   char * tempBuf = (char *) malloc(strlen(tupleString)+1);
   strcpy(tempBuf,tupleString); // copy is safer, don't know if I can modify tuple->value			    
   *sh=*sm=*eh=*em=0;
   *own = false;
   char * sep = strchr(tempBuf, '|'); //first |   18:30|20:20|1
   int res = 1; //error
   if (sep != 0)
   {
     
     char * sep2 = strchr(sep+1, '|'); //second |        0     0
     *sep  = 0;
     *sep2 = 0;
     char * colon = strchr(tempBuf,':'); //first :    0     0  
     char * colon2 = strchr(sep+1,':'); //second :
     *colon = 0;
     *colon2 = 0;
     *sh = atoi(tempBuf);        // 18
     *sm = atoi(colon+1);        //    30
     *eh = atoi(sep+1);          //       20
     *em = atoi(colon2+1);       //           20
     *own   = (*(sep2+1) == '1') ;        //              1
     //hmm should work  
     res = 0; //success
   }
   free (tempBuf);
   return res;  
}

//free arrays, intended to use on customHours and customRels
	void freeArray(char **arr,int n)
	{
	  for (int i = 0; i< n; ++i)
	  {
	    if (arr[i])
	    {
	      free(arr[i]);
	      arr[i]=0;
      }
    }
	}  
//die verkettete Liste freigeben
static void freeTT()
{
  APP_LOG(APP_LOG_LEVEL_DEBUG,"Free List of TimeTable Entries");
  for (int i = 0; i < 7; ++i)
  {   
    TTEntry *next = ttPerDay[i];
    ttPerDay[i] = 0;
    TTEntry *akt = next;
 
    while(akt)
    {
       next = akt->next;
       free(akt); 
       akt = next;
    }
  }
}
static void logTT(const char * fromWhere)
{
  APP_LOG(APP_LOG_LEVEL_DEBUG," List of TimeTable Entries we are called from %s",fromWhere);
  for (int i = 0; i < 7; ++i)
  {   
    TTEntry * akt = ttPerDay[i];
    APP_LOG(APP_LOG_LEVEL_DEBUG,"We have day number %i with pointer %i",i,(int) akt);
 
    while(akt)
    {
       APP_LOG(APP_LOG_LEVEL_DEBUG,"We have startMin %i endmin %i position %i  and next %i",akt->startMin, 
         akt->endMin, akt->position, (int) akt->next);
       akt = akt->next;
    }
  }
}

//put data to list 
void put_to_list(int key, int sh, int sm, int eh, int em, bool own)
{
  unsigned int dayIndex = (unsigned int) key / 1000 - 1;
  unsigned int position = (unsigned int) key - key/1000 * 1000;  
  
  TTEntry * tt = (TTEntry *) malloc(sizeof(TTEntry));
  tt->startMin = sh*60 + sm;
  tt->endMin = eh*60 +em;
  tt->own = own;
  tt->next = 0;
  tt->position = position;   
  TTEntry * aktTT = ttPerDay[dayIndex];
  TTEntry * prev = 0; 
  //int counter = 0;
  while (  aktTT && aktTT->next && position > aktTT->position) //position suchen
  {
    prev = aktTT;
    aktTT = aktTT->next;
    
  }
  //problem: in the versions up to 1.4 I can delete the list before I come here
  //now since 1.5 it isn't possible 
  //position == aktTT->position || position < aktTT->position, if == the entry could exist
  //ok, but does not solve the problem cause now I can't delete an enry
  //I must detect if we have configuration from setting and in this case delete the list of timetable entries 
 // if (aktTT && aktTT->position == tt->position 
 //           && aktTT->startMin == tt->startMin 
 //           && aktTT->endMin   == tt->endMin
 //           && aktTT->own      == tt->own)
 // {
 // }
 // else
 // {   
    //habe :0 -> erstes Element / aktTT->next == 0 -> muss als letztes Element gesetzt werde / einbauen
    if (!aktTT || (!prev && position <= aktTT->position)  ) //erstes
    {
      ttPerDay[dayIndex] = tt;
      tt->next = aktTT;			    
    }
    else if (!aktTT->next && position > aktTT->position) //letztes
      aktTT->next = tt;
    else //einbauen
    { 
      tt->next = aktTT;
      prev->next = tt;
    } //jetzt sollte eingehaengt sein
 // } 
}          

//log array content 
void log_array(char ** arrOfC, int n )
{
  for (int i = 0; i< n; ++i)
  {
    if (arrOfC[i]) //problem with german umlauts - raus 
      APP_LOG(APP_LOG_LEVEL_DEBUG,"found entry %d ", i);
    else
      APP_LOG(APP_LOG_LEVEL_DEBUG,"entry %d is not set", i);
  }
}
//put string in the form bla | blub and blob | xyz to array
//n is number of words in the array arrOfC, n char * must be
//present in the array
//have array of given size of type char *
void string_to_array(char ** arrOfC, int n, char *string)
{
   freeArray(arrOfC,n);
   char * akt = strchr(string, '|'); 
   char * prev = string;
   int i = 0; 
   while (akt && i < n-1)
   {
       int size = akt - prev;
       if (arrOfC[i])       
         free(arrOfC[i]);
       arrOfC[i] = (char * ) calloc(size+1, sizeof(char));
       strncpy(arrOfC[i++],prev, size);
       prev = akt + 1; 
       akt = strchr(akt+1,'|');
   }
   //letzten holen
   int size = string + strlen(string) - prev;
   if (arrOfC[i])
     free(arrOfC[i]);
   arrOfC[i] = (char * ) calloc(size+1, sizeof(char));
   strcpy(arrOfC[i++],prev); 
   //log_array(arrOfC,i);
}

//tap should switch to data mode - only numbers
static void tap_handler(AccelAxisType axis, int32_t direction) {
  dataMode = !dataMode;
  //have problems when using pebble time, actual_time is not given?
  //on old pebble no such problems - maybe the reason is located in starting the app?
	time_t raw_time;
	time(&raw_time);
	actual_time = localtime(&raw_time); //where is memory handled
      
  if (actual_time)
    handle_minute_tick(actual_time,MINUTE_UNIT);
}



//tuple belongs to an iterator, and it is possible that the
//space is reused after the receive event is finished
//so we need to store the values 
static void process_key_value(Tuple *tuple)
{
  uint32_t key =  tuple->key;

	GTextAlignment alignment;
	switch (key) {
		case TEXT_ALIGN_KEY:
			text_align = tuple->value->uint8;
			persist_write_int(TEXT_ALIGN_KEY, text_align);

			alignment = lookup_text_alignment(text_align);
			for (int i = 0; i < NUM_LINES; i++)
			{
				text_layer_set_text_alignment(lines[i].currentLayer, alignment);
				text_layer_set_text_alignment(lines[i].nextLayer, alignment);
				layer_mark_dirty(text_layer_get_layer(lines[i].currentLayer));
				layer_mark_dirty(text_layer_get_layer(lines[i].nextLayer));
			}
			break;
    #ifdef PBL_BW
		case INVERT_KEY:
			invert = tuple->value->uint8 == 1;
			persist_write_bool(INVERT_KEY, invert);
			layer_set_hidden(inverter_layer_get_layer(inverter_layer), !invert);
			layer_mark_dirty(inverter_layer_get_layer(inverter_layer));
			break;
    #endif
		case LANGUAGE_KEY:
			lang = (Language) tuple->value->uint8;
			persist_write_int(LANGUAGE_KEY, lang);
			break;
			//extensions for v 1.3
		case DELTA_KEY:
			delta = tuple->value->uint8 == 1;
			persist_write_bool(DELTA_KEY, delta); //persistent
			break;
		case BATTERY_KEY:
			battery = tuple->value->uint8 == 1;
			persist_write_bool(BATTERY_KEY, battery); //persistent
			break;
		case BATTERY_PHONE_KEY:
			batteryPhone = tuple->value->uint8 == 1;
			persist_write_bool(BATTERY_PHONE_KEY, batteryPhone); //persistent
			break;
		case WARNOWN_KEY:
			warnown = tuple->value->uint8 == 1;
			persist_write_bool(WARNOWN_KEY, warnown); //persistent
			break;
		case DONE_KEY:
		  done = tuple->value->uint8 == 1;
			persist_write_bool(DONE_KEY, done); //persistent
			break;
		case TIMETABLE_KEY:
			tt_entries = tuple->value->uint32;
			persist_write_int(TIMETABLE_KEY, tt_entries); //persistent
			if (!tt_entries)
			  freeTT(); //free it 
			break;
		case HOURS_KEY: //long string of hours, separated by |
		  persist_write_string(HOURS_KEY, tuple->value->cstring);		  
		  string_to_array(customHours, 24, tuple->value->cstring);
		  break;	
		case RELS_KEY:
		  persist_write_string(RELS_KEY, tuple->value->cstring);
		  string_to_array(customRels, 12, tuple->value->cstring);
		  break;
    case SHAKE_KEY: 
			if (shakeDetect)
  			accel_tap_service_unsubscribe();
      shakeDetect = tuple->value->uint8 == 1;
			persist_write_bool(SHAKE_KEY, shakeDetect); //persistent
			if (shakeDetect)
        accel_tap_service_subscribe(tap_handler);
			else
			  dataMode = false; //false however it was before
			break;
		case BATTERY_LEVEL_KEY: 
		  batteryPhoneLevel = tuple->value->uint8;
		  break;	
	  default:
	    //store raw c-string 
			// must be TTEntry ,
			// get var dayKeys = [1000,2000,3000,4000,5000,6000,7000]
			if ((unsigned int)key >= 1000)
			{
			  persist_write_string((int)key,tuple->value->cstring);
        int sh, sm, eh, em;
        bool own;
        if (! getTimeData((char *) tuple->value->cstring, &sh, &sm, &eh, &em, &own)) // returns 0 for success
        {
        //APP_LO G(APP_LOG_LEVEL_DEBUG,"sh %i sm %i eh %i em %i own %i ", sh, sm, eh,em, own );
          put_to_list(key, sh, sm, eh, em, own);
        }
			}
			else
			{
			  APP_LOG(APP_LOG_LEVEL_DEBUG,"problems with key %u", (unsigned int) key);
			}
	}
	if (actual_time)//actual_time seems not to be persistent, so this is
	//wrong on pebble_time
	{
  	time_t raw_time;
  	time(&raw_time);
  	actual_time = localtime(&raw_time); //where is memory handled
	  
	  handle_minute_tick(actual_time,MINUTE_UNIT);
	}
}
//event-handlers
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  //callback can only mean that we receive our settings
  //logTT("inbox_received_callback");
  //freeTT(); was okay when only receiving configuration, but now we receive information
  //of phone battery  status and then we can't delete the timeTable here 
  //hmm, need to destroy timetable list if the received data results from configuration
  // can check this by find in dictionary, 
  Tuple *t = dict_find(iterator,BATTERY_LEVEL_KEY);
  if (! t)
  {
     freeTT(); //should it be, hmm 
     APP_LOG(APP_LOG_LEVEL_DEBUG,"caused by save from pebble app"); 
  }
  else 
     APP_LOG(APP_LOG_LEVEL_DEBUG,"found battery phone key"); 
  t = dict_read_first(iterator);
  // Process all pairs present
  while (t != NULL) {
    // Long lived buffer
    process_key_value(t);
    // Process this pair's key
    // Get next pair, if any
    t = dict_read_next(iterator);
  }
	//have read all
	//logTT("inbox_received - done");
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
  //in the moment it can be only the battery request 
  batteryPhoneLevel = -1;
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Outbox send success!");
}
//switch to app_message instead of app_synch
static void handle_message_init()
{

   //register event handler
   
   app_message_register_inbox_received(inbox_received_callback);
   app_message_register_inbox_dropped(inbox_dropped_callback);
   app_message_register_outbox_failed(outbox_failed_callback);
   app_message_register_outbox_sent(outbox_sent_callback);
   
   // Initialize message queue
  
   APP_LOG(APP_LOG_LEVEL_INFO,"max outbox %u max inbox %u garantie outbox %u garantie inbox %u",
	                    (uint) app_message_outbox_size_maximum(),
	                    (uint) app_message_inbox_size_maximum(),
	                    (uint) APP_MESSAGE_OUTBOX_SIZE_MINIMUM,
	                    (uint) APP_MESSAGE_INBOX_SIZE_MINIMUM
	                      );
   APP_LOG(APP_LOG_LEVEL_INFO,"if app_message_open uses below garantie, it's sure that it will work, we use max values");                      
  
   app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
                                       
}
        
static void window_load(Window *window)
{
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_frame(window_layer);
	//ok, get x 0 y 0 w 144 h 168
	// Init and load lines
	for (int i = 0; i < NUM_LINES; i++)
	{
		init_line(&lines[i]);
		layer_add_child(window_layer, (Layer *)lines[i].currentLayer);
		layer_add_child(window_layer, (Layer *)lines[i].nextLayer);
	}
	tempLayer = text_layer_create(GRect(144, 0, 144, 50));//besser hier, performance?
	text_layer_set_text(tempLayer,tempLayerText); //sonst hat er keinen speicher
	layer_add_child(window_layer, (Layer *)tempLayer);

  deltaLayer = prepareTextLayer (window_layer, deltaLayerText, GTextAlignmentCenter, 0,0,144,28);//x,y,w,h
  doneLayerL = prepareTextLayer (window_layer, doneLayerTextL,GTextAlignmentLeft, 0,0,144,28);  
  doneLayerR = prepareTextLayer (window_layer, doneLayerTextR,GTextAlignmentRight,94,0,50,28);  

  batteryLayer = layer_create(GRect(0,5,144,34)); //von 35 bis 36
  layer_set_update_proc(batteryLayer, batteryDisplayUpdate);
  layer_add_child(window_layer,batteryLayer);
  batteryDisplay();
#ifdef PBL_BW
	inverter_layer = inverter_layer_create(bounds);
	layer_set_hidden(inverter_layer_get_layer(inverter_layer), !invert);
	layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));
#endif
	// Configure time on init
	    
	time_t raw_time;
	time(&raw_time);
	actual_time = localtime(&raw_time); //where is memory handled
	display_actual_time(actual_time);	
  //message gedoens
  

}

static void window_unload(Window *window)
{
	// Free layers
	#ifdef PBL_BW
	inverter_layer_destroy(inverter_layer);
  #endif
  text_layer_destroy(tempLayer);
	for (int i = 0; i < NUM_LINES; i++)
	{
		destroy_line(&lines[i]);
	}
}


static void handle_init() {
	// Load settings from persistent storage
  /*remember 
    #define TIMETABLE_KEY 0 //how many entries in timetable
    #define TEXT_ALIGN_KEY 1
    #define LANGUAGE_KEY 2
    #define DELTA_KEY 3   
    #define DONE_KEY 4  //show minutes done   
    #define BATTERY_KEY 5 // battery bar 
    #define WARNOWN_KEY 6 // warning / vibration 5 minutes before own lessen 
    #define INVERT_KEY 10
    #define BATTERY_PHONE_KEY 11  
  */
	if (persist_exists(TIMETABLE_KEY))
	{ 
    tt_entries = persist_read_int(TIMETABLE_KEY);
	}
	if (persist_exists(TEXT_ALIGN_KEY))
	{
		text_align = persist_read_int(TEXT_ALIGN_KEY);
	}
	if (persist_exists(INVERT_KEY))
	{
		invert = persist_read_bool(INVERT_KEY);
	}
	if (persist_exists(LANGUAGE_KEY))
	{
		lang = (Language) persist_read_int(LANGUAGE_KEY);
	}
	if (persist_exists(DELTA_KEY))
	{
		delta =  persist_read_bool(DELTA_KEY);
	}
	if (persist_exists(DONE_KEY))
	{
		done =  persist_read_bool(DONE_KEY);
	}
	if (persist_exists(BATTERY_KEY))
	{
		battery =  persist_read_bool(BATTERY_KEY);
	}
	if (persist_exists(BATTERY_PHONE_KEY))
	{
		batteryPhone =  persist_read_bool(BATTERY_PHONE_KEY);
	}
	if (persist_exists(WARNOWN_KEY))
	{
		warnown =  persist_read_bool(WARNOWN_KEY);
	}
	if (persist_exists(SHAKE_KEY))
	{
		shakeDetect =  persist_read_bool(SHAKE_KEY);
	}
	
	if (persist_exists(HOURS_KEY))
	{
        int size = persist_get_size(HOURS_KEY);
        char * tempBuf = (char *) malloc(size+1);	  
        persist_read_string(HOURS_KEY,tempBuf,size+1);
        string_to_array(customHours, 24, tempBuf);
        free(tempBuf);
	}
	if (persist_exists(RELS_KEY))
	{
        int size = persist_get_size(RELS_KEY);
        char * tempBuf = (char *) malloc(size+1);	  
        persist_read_string(RELS_KEY,tempBuf,size+1);
        string_to_array(customRels, 12, tempBuf);
        free(tempBuf);
	}
	
	int base = 1000;
	int perDayCounter = 0;
	int totalCounter = 0;
	char buffer[64];
  while ( totalCounter < tt_entries &&  base < 8000)
  {
    int key = base + perDayCounter++;
    if (persist_exists(key))
    {
      persist_read_string(key,buffer,64);
      int sh, sm, eh, em;
      bool own;
      if (! getTimeData((char *) buffer, &sh, &sm, &eh, &em, &own)) // returns 0 for success
      {
      //APP_LOG(APP_LOG_LEVEL_DEBUG,"sh %i sm %i eh %i em %i own %i ", sh, sm, eh,em, own );
        put_to_list(key, sh, sm, eh, em, own);
      }
      totalCounter++;  
    }
    else
    {
      base += 1000;
      perDayCounter=0;
    }
  }
  //logTT("handle_init");
 	//log_array(customHours,24);
 	//log_array(customRels,12);
  
	window = window_create();
	window_set_background_color(window, GColorBlack);
	window_set_window_handlers(window, (WindowHandlers) {
		.load = window_load,
		.unload = window_unload
	});
	#ifdef PBL_BW
  window_set_fullscreen(window, true); 
  #endif 
  //subscribe tap_handler
  if (shakeDetect)
    accel_tap_service_subscribe(tap_handler);

	const bool animated = true;
	window_stack_push(window, animated);

	// Subscribe to minute ticks
	tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
#if DEBUG
	// Button functionality
	window_set_click_config_provider(window, (ClickConfigProvider) click_config_provider);
#endif
 handle_message_init();	
}

static void handle_deinit()
{
  
  if (shakeDetect)
    accel_tap_service_unsubscribe(); //not sure if necessary, but in complete example they use unsubscribe
	freeTT(); //list
	freeArray(customHours,24); //array with custom Hours
	freeArray(customRels,12);
	// Free window	
	window_destroy(window);
}

int main(void)
{
	handle_init();
	app_event_loop();
	handle_deinit();
}

