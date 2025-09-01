#include <iostream>
#include <string>
#include <memory>
#include <cstdio>

#include "processing/MediaProcessor.h"

void MediaProcessor::process(const std::string& url) {
  std::string ffmpeg_cmd = "ffmpeg -i \"" + url + "\" -vcodec libx264 -crf 23 -preset fast -acodec aac -f mp4 pipe:1";
  FILE* pipe = popen(ffmpeg_cmd.c_str(), "r");
  if (!pipe) {
      std::cerr << "Failed to start ffmpeg process" << std::endl;
      return;
  }

  // Read ffmpeg output stream here
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      // Process compressed data stream chunk from FFmpeg
  }
  pclose(pipe);
}