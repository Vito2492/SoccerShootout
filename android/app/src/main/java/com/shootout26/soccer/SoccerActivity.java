package com.shootout26.soccer;

import org.libsdl.app.SDLActivity;

public class SoccerActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
