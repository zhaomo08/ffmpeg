#include <SDL.h>
#include <stdio.h>


//   clang -g -o firstsdl firstsdl.c `pkg-config --cflags  --libs sdl2`
// ./firstsld

int main(int argc,char *argv[]){


    SDL_Window *window = NULL;

    SDL_Renderer *render = NULL;

    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("SDL2 Window",
                    200,
                    200,
                    640,
                    480,
                    SDL_WINDOW_SHOWN);


    if (!window){
        printf("Failed to Create window!");
        goto _EXIT;
    }
    render = SDL_CreateRenderer(window,-1,0);

    if (!render){
        SDL_Log("Failed to  Create Render");
        goto _DWINDOW;
    }

    SDL_SetRenderDrawColor(render,255,0,0,255);

    SDL_RenderClear(render);


    SDL_RenderPresent(render);

    SDL_Delay(30000);

  
    
_DWINDOW:
    SDL_DestroyWindow(window);
_EXIT:    
    SDL_Quit();

    return 0;
} 