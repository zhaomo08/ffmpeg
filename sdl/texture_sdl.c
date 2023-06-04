#include <SDL.h>
#include <stdio.h>


//   clang -g -o texture_sdl texture_sdl.c `pkg-config --cflags  --libs sdl2`
// ./texture_sdl

int main(int argc,char *argv[]){


    int quit = 1;
    SDL_Event event;

    SDL_Texture *texture = NULL;

    SDL_Rect rect;
    rect.w = 30;
    rect.h = 30;

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

    // SDL_SetRenderDrawColor(render,255,0,0,255);

    // SDL_RenderClear(render);

    // SDL_RenderPresent(render);

    texture = SDL_CreateTexture(render,
                      SDL_PIXELFORMAT_RGBA8888,
                      SDL_TEXTUREACCESS_TARGET,
                      600,
                      480);

    if (!texture){
        SDL_Log("failed to create texture");
        goto _RENDER;
    }
    

    do{
        SDL_PollEvent(&event);  
        switch (event.type)
        {
        case SDL_QUIT:
            quit = 0;
            break;
        
        default:
            SDL_Log("event type is %d",event.type);
        }

        
        rect.x = rand() % 600;
        rect.y = rand() % 450;

        SDL_SetRenderTarget(render, texture);
        SDL_SetRenderDrawColor(render,0,0,0,0);
        SDL_RenderClear(render);

        SDL_RenderDrawRect(render,&rect);
        SDL_SetRenderDrawColor(render,255,0,0,0);
        SDL_RenderFillRect(render,&rect);

        SDL_SetRenderTarget(render,NULL);
        // 这一步  注释掉  也是不展示红色小方块   
        SDL_RenderCopy(render,texture,NULL,NULL);
        // 注释掉下面一行 小红块不出现 但是显卡层 已经计算完了
        SDL_RenderPresent(render);


    }while (quit); 

    

   

    SDL_DestroyTexture(texture);
     // SDL_Delay(30000);

_RENDER:
    SDL_DestroyRenderer(render);
_DWINDOW:
    SDL_DestroyWindow(window);
_EXIT:    
    SDL_Quit();
    return 0;
} 