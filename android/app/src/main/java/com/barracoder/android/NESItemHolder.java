package com.barracoder.android;

import android.view.View;
import android.widget.Button;
import android.widget.ImageButton;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.recyclerview.widget.RecyclerView;

public class NESItemHolder extends RecyclerView.ViewHolder {
    ImageView gameImage;
    TextView gameName;
    TextView gameDetails; // Novo campo
    Button playButton;
    ImageButton magicButton;
    NESItemModel item;

    public NESItemHolder(View itemView){
        super(itemView);
        gameImage = itemView.findViewById(R.id.gameImage);
        gameName = itemView.findViewById(R.id.gameName);
        gameDetails = itemView.findViewById(R.id.gameDetails); // Bind do novo ID
        playButton = itemView.findViewById(R.id.playButton);
        magicButton = itemView.findViewById(R.id.magicButton);
    }
}
