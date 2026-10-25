# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake VideoComponent. It writes the playlist to the owner, as the game does."""


class VideoComponent:
    def __init__(self, owner, mute_speed=3, distortion_speed=3):
        self.owner = owner
        self.video_display_type = 0
        self.mute_speed = mute_speed
        self.distortion_speed = distortion_speed
        self.speed_audio_clip_replacement = None
        self.calls = []

    def set_video_clips(self, clip_names=[], loop_last=False, append_clip=False):
        self.calls.append(("set", list(clip_names), loop_last))
        self.owner.video_playlist = list(clip_names) or None

    def add_video_clips(self, clip_names, loop_last=False):
        self.calls.append(("add", list(clip_names), loop_last))

    def on_remove(self):
        self.calls.append(("remove",))
