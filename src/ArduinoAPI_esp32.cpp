#include <string.h>

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "bb_spi_lcd.h"

#include "ArduinoAPI.h"
#include "ArduinoDraw.h"

#define DisplayWidth 240
#define DisplayHeight 240

const int Pin_CS = -1;
const int Pin_DC = 2;
const int Pin_RST = 0;
const int Pin_MISO = -1;
const int Pin_MOSI = 13;
const int Pin_CLK = 14;

bool SerMouseDown = false;
int SerMouseDX = 0;
int SerMouseDY = 0;

int MouseX = 0;
int MouseY = 0;

#define DrawScreenEvent 0x01

EventGroupHandle_t RenderTaskEventHandle = NULL;
SemaphoreHandle_t RenderTaskLock = NULL;
TaskHandle_t RenderTaskHandle = NULL;

volatile const uint8_t* EmScreenPtr = NULL;

void PrintCore( const char* Str ) {
    Serial.print( Str );
    Serial.print( " is on core " );
    Serial.println( xPortGetCoreID( ) );
}

void RenderTask( void* Param ) {
    int x = 0;
    int y = 0;

    PrintCore( __func__ );

    while ( true ) {
        x = MouseX - ( DisplayWidth / 2 );
        y = MouseY - ( DisplayHeight / 2 );

        x = x < 0 ? 0 : x;
        y = y < 0 ? 0 : y;

        xEventGroupWaitBits( RenderTaskEventHandle, DrawScreenEvent, pdTRUE, pdTRUE, portMAX_DELAY );

        xSemaphoreTake( RenderTaskLock, portMAX_DELAY );
            DrawWindowSubpixel( ( const uint8_t* ) EmScreenPtr, x, y );
        xSemaphoreGive( RenderTaskLock );
    }
}

void setup( void ) {
    Serial.begin( 115200 );

    spilcdInit( LCD_ST7789_NOCS, 0, 0, 0, 40000000, Pin_CS, Pin_DC, Pin_RST, -1, Pin_MISO, Pin_MOSI, Pin_CLK );
    spilcdFill( 0, 1 );

    SPIFFS.begin( );

    Setup1BPPTable( );
    SetupScalingTable( );

    pinMode( 4, OUTPUT );

    RenderTaskEventHandle = xEventGroupCreate( );
    RenderTaskLock = xSemaphoreCreateMutex( );

    xTaskCreatePinnedToCore( RenderTask, "RenderTask", 4096, NULL, 0, &RenderTaskHandle, 0 );

    Serial.println( ( int ) RenderTaskEventHandle, 16 );
    Serial.println( ( int ) RenderTaskLock, 16 );
    Serial.println( ( int ) RenderTaskHandle );

    PrintCore( __func__ );

    Serial.print( "Freq: " );
    Serial.println( ( int ) getCpuFrequencyMhz( ) ); 
}

void loop( void ) {
    PrintCore( __func__ );

    minivmac_main( 0, NULL );

    Serial.println( "If we got here, something bad happened." );

    while ( true ) {
        delay( 1000 );
    }
}

void ArduinoAPI_GetDisplayDimensions( int* OutWidthPtr, int* OutHeightPtr ) {
    *OutWidthPtr = DisplayWidth;
    *OutHeightPtr = DisplayHeight;
}

void ArduinoAPI_SetAddressWindow( int x0, int y0, int x1, int y1 ) {
    spilcdSetPosition( x0, y0, ( x1 - x0 ), ( y1 - y0 ), 1 );
}

void ArduinoAPI_WritePixels( const uint16_t* Pixels, size_t Count ) {
    spilcdWriteDataBlock( ( uint8_t* ) Pixels, Count * sizeof( uint16_t ), 1 );
}

void ArduinoAPI_GetMouseDelta( int* OutXDeltaPtr, int* OutYDeltaPtr ) {
    *OutXDeltaPtr = SerMouseDX;
    *OutYDeltaPtr = SerMouseDY;

    SerMouseDX = 0;
    SerMouseDY = 0;
}

void ArduinoAPI_GiveEmulatedMouseToArduino( int* EmMouseX, int* EmMouseY ) {
    MouseX = *EmMouseX;
    MouseY = *EmMouseY;
}

int ArduinoAPI_GetMouseButton( void ) {
    return SerMouseDown;
}

uint64_t ArduinoAPI_GetTimeMS( void ) {
    return ( uint64_t ) 1591551981844L + ( uint64_t ) millis( );
}

void ArduinoAPI_Yield( void ) {
    yield( );
}

void ArduinoAPI_Delay( uint32_t MSToDelay ) {
    delay( MSToDelay );
}

ArduinoFile ArduinoAPI_open( const char* Path, const char* Mode ) {
    char SPIFFSPath[ 256 ];

    snprintf( SPIFFSPath, sizeof( SPIFFSPath ), "/spiffs/%s", Path );
    return ( ArduinoFile ) fopen( SPIFFSPath, Mode );
}

void ArduinoAPI_close( ArduinoFile Handle ) {
    if ( Handle ) {
        fclose( ( FILE* ) Handle );
    }
}

size_t ArduinoAPI_read( void* Buffer, size_t Size, size_t Nmemb, ArduinoFile Handle ) {
    size_t BytesRead = 0;

    if ( Handle ) {
        BytesRead = fread( Buffer, Size, Nmemb, ( FILE* ) Handle );
    }

    return BytesRead;
}

size_t ArduinoAPI_write( const void* Buffer, size_t Size, size_t Nmemb, ArduinoFile Handle ) {
    size_t BytesWritten = 0;

    if ( Handle ) {
        BytesWritten = fwrite( Buffer, Size, Nmemb, ( FILE* ) Handle );
    }

    return BytesWritten;
}

long ArduinoAPI_tell( ArduinoFile Handle ) {
    long Offset = 0;

    if ( Handle ) {
        Offset = ftell( ( FILE* ) Handle );
    }

    return Offset;
}

long ArduinoAPI_seek( ArduinoFile Handle, long Offset, int Whence ) {
    if ( Handle ) {
        return fseek( ( FILE* ) Handle, Offset, Whence );
    }

    return -1;
}

int ArduinoAPI_eof( ArduinoFile Handle ) {
    if ( Handle ) {
        return feof( ( FILE* ) Handle );
    }

    return 0;
}

void* ArduinoAPI_malloc( size_t Size ) {
    return heap_caps_malloc( Size, ( Size >= 262144 ) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT );
}

void* ArduinoAPI_calloc( size_t Nmemb, size_t Size ) {
    return heap_caps_calloc( Nmemb, Size, ( ( Size * Nmemb ) >= 262144 ) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_DEFAULT );
}

void ArduinoAPI_free( void* Memory ) {
    heap_caps_free( Memory );
}

void ArduinoAPI_CheckForEvents( void ) {
    while ( Serial.available( ) ) {
        switch ( Serial.read( ) ) {
            case 'w': {
                SerMouseDY-= 2;
                break;
            }
            case 's': {
                SerMouseDY+= 2;
                break;
            }
            case 'a': {
                SerMouseDX-= 2;
                break;
            }
            case 'd': {
                SerMouseDX+= 2;
                break;
            }
            case ' ': {
                SerMouseDown = ! SerMouseDown;
                break;
            }
            default: break;
        }
    }
}

bool Changed = false;

void ArduinoAPI_ScreenChanged( int Top, int Left, int Bottom, int Right ) {
    Changed = true;
}

void ArduinoAPI_DrawScreen( const uint8_t* Screen ) {
    if ( Changed ) {
        EmScreenPtr = Screen;
        Changed = false;

        if ( xSemaphoreTake( RenderTaskLock, 0 ) == pdTRUE ) {
            xSemaphoreGive( RenderTaskLock );

            xEventGroupSetBits( RenderTaskEventHandle, DrawScreenEvent );
        }
    }
}