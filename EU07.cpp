/*
This Source Code Form is subject to the
terms of the Mozilla Public License, v.
2.0. If a copy of the MPL was not
distributed with this file, You can
obtain one at
http://mozilla.org/MPL/2.0/.
*/
/*
    MaSzyna EU07 locomotive simulator
    Copyright (C) 2001-2004  Marcin Wozniak, Maciej Czapkiewicz and others
*/
/*
Authors:
MarcinW, McZapkie, Shaxbee, ABu, nbmx, youBy, Ra, winger, mamut, Q424,
Stele, firleju, szociu, hunter, ZiomalCl, OLI_EU and others
*/

#include "stdafx.h"
#include <png.h>
#include <thread>
#include <direct.h>

#include "Globals.h"
#include "Logs.h"
#include "keyboardinput.h"
#include "gamepadinput.h"
#include "Console.h"
#include "PyInt.h"
#include "World.h"
#include "Mover.h"
#include "usefull.h"
#include "timer.h"
#include "resource.h"
#include "uilayer.h"

#pragma comment (lib, "glu32.lib")
#pragma comment (lib, "dsound.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "setupapi.lib")
#pragma comment (lib, "dbghelp.lib")
#pragma comment (lib, "version.lib")

TWorld World;

namespace input {

keyboard_input Keyboard;
gamepad_input Gamepad;

}

void screenshot_save_thread( char *img )
{
	png_image png;
	memset(&png, 0, sizeof(png_image));
	png.version = PNG_IMAGE_VERSION;
	png.width = Global::ScreenWidth;
	png.height = Global::ScreenHeight;
	png.format = PNG_FORMAT_RGB;

	char datetime[64];
	time_t timer;
	struct tm* tm_info;
	time(&timer);
	tm_info = localtime(&timer);
	strftime(datetime, 64, "%Y-%m-%d_%H-%M-%S", tm_info);

	uint64_t perf;
	QueryPerformanceCounter((LARGE_INTEGER*)&perf);

	std::string filename = "screenshots/" + std::string(datetime) +
	                       "_" + std::to_string(perf) + ".png";

	if (png_image_write_to_file(&png, filename.c_str(), 0, img, -Global::ScreenWidth * 3, nullptr) == 1)
		WriteLog("saved " + filename + ".");
	else
		WriteLog("failed to save screenshot.");

	delete[] img;
}

void make_screenshot()
{
	char *img = new char[Global::ScreenWidth * Global::ScreenHeight * 3];
	glReadPixels(0, 0, Global::ScreenWidth, Global::ScreenHeight, GL_RGB, GL_UNSIGNED_BYTE, (GLvoid*)img);

	std::thread t(screenshot_save_thread, img);
	t.detach();
}

void window_resize_callback(GLFWwindow *window, int w, int h)
{
    // NOTE: we have two variables which basically do the same thing as we don't have dynamic fullscreen toggle
    // TBD, TODO: merge them?
	Global::ScreenWidth = Global::iWindowWidth = w;
	Global::ScreenHeight = Global::iWindowHeight = h;
    Global::fDistanceFactor = std::max( 0.5f, h / 768.0f ); // not sure if this is really something we want to use
	glViewport(0, 0, w, h);
}

void cursor_pos_callback(GLFWwindow *window, double x, double y)
{
    input::Keyboard.mouse( x, y );
#ifdef EU07_USE_OLD_COMMAND_SYSTEM
	World.OnMouseMove(x * 0.005, y * 0.01);
#endif
	glfwSetCursorPos(window, 0.0, 0.0);
}

void key_callback( GLFWwindow *window, int key, int scancode, int action, int mods )
{
    input::Keyboard.key( key, action );

    Global::shiftState = ( mods & GLFW_MOD_SHIFT ) ? true : false;
    Global::ctrlState = ( mods & GLFW_MOD_CONTROL ) ? true : false;

    if( ( key == GLFW_KEY_LEFT_SHIFT )
     || ( key == GLFW_KEY_LEFT_CONTROL )
     || ( key == GLFW_KEY_LEFT_ALT )
     || ( key == GLFW_KEY_RIGHT_SHIFT )
     || ( key == GLFW_KEY_RIGHT_CONTROL )
     || ( key == GLFW_KEY_RIGHT_ALT ) ) {
        // don't bother passing these
        return;
    }

    if( action == GLFW_PRESS || action == GLFW_REPEAT )
    {
        World.OnKeyDown( key );

        switch( key )
        {

            case GLFW_KEY_F11: {
                make_screenshot();
                break;
            }

            default: { break; }
        }
    }
}

void focus_callback( GLFWwindow *window, int focus )
{
    if( Global::bInactivePause ) // jeśli ma być pauzowanie okna w tle
        if( focus )
            Global::iPause &= ~4; // odpauzowanie, gdy jest na pierwszym planie
        else
            Global::iPause |= 4; // włączenie pauzy, gdy nieaktywy
}

void scroll_callback( GLFWwindow* window, double xoffset, double yoffset ) {

    if( Global::ctrlState ) {
        // ctrl + scroll wheel adjusts fov in debug mode
        Global::FieldOfView = clamp( static_cast<float>(Global::FieldOfView - yoffset * 20.0 / Global::fFpsAverage), 15.0f, 75.0f );
    }
}

#ifdef _WINDOWS
extern "C"
{
    GLFWAPI HWND glfwGetWin32Window(GLFWwindow* window);
}

LONG CALLBACK unhandled_handler(::EXCEPTION_POINTERS* e);
LRESULT APIENTRY WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern HWND Hwnd;
extern WNDPROC BaseWindowProc;
#endif

int main(int argc, char *argv[])
{
#ifdef _WINDOWS
	::SetUnhandledExceptionFilter(unhandled_handler);
#endif

	if (!glfwInit())
		return -1;

#ifdef _WINDOWS
    DeleteFile( "log.txt" );
    DeleteFile( "errors.txt" );
    mkdir("logs");
#endif
    Global::LoadIniFile("eu07.ini");
    Global::InitKeys();

    // hunter-271211: ukrywanie konsoli
    if( Global::iWriteLogEnabled & 2 )
	{
        AllocConsole();
        SetConsoleTextAttribute( GetStdHandle( STD_OUTPUT_HANDLE ), FOREGROUND_GREEN );
    }

	Global::asVersion = "NG";

	for (int i = 1; i < argc; ++i)
	{
		std::string token(argv[i]);

		if (token == "-modifytga")
			Global::iModifyTGA = -1;
		else if (token == "-e3d")
		{
			if (Global::iConvertModels > 0)
				Global::iConvertModels = -Global::iConvertModels;
			else
				Global::iConvertModels = -7; // z optymalizacją, bananami i prawidłowym Opacity
		}
		else if (i + 1 < argc && token == "-s")
			Global::SceneryFile = std::string(argv[++i]);
		else if (i + 1 < argc && token == "-v")
		{
			std::string v(argv[++i]);
			std::transform(v.begin(), v.end(), v.begin(), ::tolower);
			Global::asHumanCtrlVehicle = v;
		}
		else
		{
			std::cout << "usage: " << std::string(argv[0]) << " [-s sceneryfilepath] "
				      << "[-v vehiclename] [-modifytga] [-e3d]" << std::endl;
			return -1;
		}
	}

    // match requested video mode to current to allow for
    // fullwindow creation when resolution is the same
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *vmode = glfwGetVideoMode(monitor);

    glfwWindowHint(GLFW_RED_BITS, vmode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, vmode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, vmode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, vmode->refreshRate);

    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    if( Global::iMultisampling > 0 ) {
        glfwWindowHint( GLFW_SAMPLES, 1 << Global::iMultisampling );
    }

    if (Global::bFullScreen)
	{
        // match screen dimensions with selected monitor, for 'borderless window' in fullscreen mode
        Global::iWindowWidth = vmode->width;
        Global::iWindowHeight = vmode->height;
    }

    GLFWwindow *window =
        glfwCreateWindow( Global::iWindowWidth, Global::iWindowHeight,
			Global::AppName.c_str(), Global::bFullScreen ? monitor : nullptr, nullptr );

    if (!window)
	{
        std::cout << "failed to create window" << std::endl;
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(Global::VSync ? 1 : 0); //vsync
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); //capture cursor
    glfwSetCursorPos(window, 0.0, 0.0);
    glfwSetFramebufferSizeCallback(window, window_resize_callback);
    glfwSetCursorPosCallback(window, cursor_pos_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetScrollCallback( window, scroll_callback );
    glfwSetWindowFocusCallback(window, focus_callback);
    {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        window_resize_callback(window, width, height);
    }

    if (glewInit() != GLEW_OK)
	{
        std::cout << "failed to init GLEW" << std::endl;
        return -1;
    }

#ifdef _WINDOWS
    // setup wrapper for base glfw window proc, to handle copydata messages
    Hwnd = glfwGetWin32Window( window );
    BaseWindowProc = (WNDPROC)::SetWindowLongPtr( Hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc );
    // switch off the topmost flag
    ::SetWindowPos( Hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );

    const HANDLE icon = ::LoadImage(
        ::GetModuleHandle( 0 ),
        MAKEINTRESOURCE( IDI_ICON1 ),
        IMAGE_ICON,
        ::GetSystemMetrics( SM_CXSMICON ),
        ::GetSystemMetrics( SM_CYSMICON ),
        0 );
    if( icon )
        ::SendMessage( Hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>( icon ) );
#endif

    Global::pWorld = &World; // Ra: wskaźnik potrzebny do usuwania pojazdów
    try {
        if( false == World.Init( window ) ) {
            ErrorLog( "Failed to init TWorld" );
            return -1;
        }
    }
    catch( std::bad_alloc const &Error ) {

        ErrorLog( "Critical error, memory allocation failure: " + std::string( Error.what() ) );
        return -1;
    }
	catch (std::runtime_error e)
	{
		WriteLog(e.what());
		return -1;
	}

    Console *pConsole = new Console(); // Ra: nie wiem, czy ma to sens, ale jakoś zainicjowac trzeba
/*
    if( !joyGetNumDevs() )
        WriteLog( "No joystick" );
*/
    if( Global::iModifyTGA < 0 ) { // tylko modyfikacja TGA, bez uruchamiania symulacji
        Global::iMaxTextureSize = 64; //żeby nie zamulać pamięci
        World.ModifyTGA(); // rekurencyjne przeglądanie katalogów
    }
    else {
        if( Global::iConvertModels < 0 ) {
            Global::iConvertModels = -Global::iConvertModels;
            World.CreateE3D( "models\\" ); // rekurencyjne przeglądanie katalogów
            World.CreateE3D( "dynamic\\", true );
        } // po zrobieniu E3D odpalamy normalnie scenerię, by ją zobaczyć

        Console::On(); // włączenie konsoli

        try {
            while( ( false == glfwWindowShouldClose( window ) )
                && ( true == World.Update() )
                && ( true == GfxRenderer.Render() ) ) {
                glfwPollEvents();
                if( true == Global::InputGamepad ) {
                    input::Gamepad.poll();
                }
            }
        }
        catch( std::bad_alloc const &Error ) {

            ErrorLog( "Critical error, memory allocation failure: " + std::string( Error.what() ) );
            return -1;
        }

        Console::Off(); // wyłączenie konsoli (komunikacji zwrotnej)
    }

	TPythonInterpreter::killInstance();
	delete pConsole;

	glfwDestroyWindow(window);
	glfwTerminate();

	return 0;
}
