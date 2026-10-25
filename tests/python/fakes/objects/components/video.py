# ******************************************************************************
# Copyright (c) 2026 Sims 4 mods developers
# ******************************************************************************

"""A fake VideoComponent and VideoPlaylist.

As in the game, the component makes a new playlist from its own values (mute_speed, distortion_speed) and writes it
to the owner. The message of the playlist reads the values of the playlist.
"""


class VideoPlaylist:
    def __init__(self, version_id, clip_names, loop_last, display_type, mute_speed, distortion_speed,
                 speed_audio_clip_replacement, append_clip):
        self.version_id = version_id
        self.clip_keys = list(clip_names)
        self.loop_last = loop_last
        self.display_type = display_type
        self.mute_speed = mute_speed
        self.distortion_speed = distortion_speed
        self.speed_audio_clip_replacement = speed_audio_clip_replacement
        self.append_clip = append_clip

    def append_clips(self, clip_names, loop_last):
        if clip_names:
            self.clip_keys += list(clip_names)
            self.loop_last = loop_last

    def get_protocol_msg(self):
        return {
            "version_id": self.version_id,
            "clip_keys": list(self.clip_keys),
            "mute_speed": int(self.mute_speed),
            "distortion_speed": int(self.distortion_speed),
        }


class VideoComponent:
    def __init__(self, owner, mute_speed=2, distortion_speed=2):
        self.owner = owner
        self.video_display_type = 0
        self.mute_speed = mute_speed
        self.distortion_speed = distortion_speed
        self.speed_audio_clip_replacement = None
        self.calls = []

    def set_video_clips(self, clip_names=[], loop_last=False, append_clip=False):
        self.calls.append(("set", list(clip_names), loop_last))
        playlist = self.owner.video_playlist
        version_id = 0 if playlist is None else playlist.version_id + 1
        self.owner.video_playlist = VideoPlaylist(version_id, clip_names, loop_last, self.video_display_type,
                                                  self.mute_speed, self.distortion_speed,
                                                  self.speed_audio_clip_replacement, append_clip)

    def add_video_clips(self, clip_names, loop_last=False):
        self.calls.append(("add", list(clip_names), loop_last))
        if not clip_names:
            return
        if self.owner.video_playlist is None:
            self.set_video_clips(clip_names, loop_last)
        else:
            self.owner.video_playlist.append_clips(clip_names, loop_last)
            self.owner._resend_video_playlist()

    def on_remove(self):
        self.calls.append(("remove",))
