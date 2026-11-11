/* Native headless asset/export/build frontend. No GTK, GL or Python dependency. */
#include "core.h"
#include "audio.h"
#include <string.h>

int main(int argc,char **argv)
{
    char *output=NULL,*asset_root=NULL,*wav=NULL,*audio_directory=NULL,*resources=NULL;
    GError *error=NULL;
    if(argc>1 && !strcmp(argv[1],"--build-ps1")) return engine_build_main(argc,argv);
    GOptionEntry entries[]={
        {"resources",0,0,G_OPTION_ARG_FILENAME,&resources,"Convert a C asset list to resource banks/TIMs","HEADER"},
        {"output",0,0,G_OPTION_ARG_FILENAME,&output,"Export directory","DIRECTORY"},
        {"asset-root",0,0,G_OPTION_ARG_FILENAME,&asset_root,"Resource asset folder","DIRECTORY"},
        {"import-wav",0,0,G_OPTION_ARG_FILENAME,&wav,"Encode PCM WAV to PS1 VAG","FILE"},
        {"audio-directory",0,0,G_OPTION_ARG_FILENAME,&audio_directory,"Destination for VAG samples","DIRECTORY"},
        {NULL,0,0,0,NULL,NULL,NULL}
    };
    GOptionContext *context=g_option_context_new("- PSX Lib (headless C89 game pipeline)");
    g_option_context_add_main_entries(context,entries,NULL);
    gboolean ok=g_option_context_parse(context,&argc,&argv,&error);
    if(!ok || argc!=1) {g_printerr("%s\n",error?error->message:"Unexpected arguments.");g_clear_error(&error);g_option_context_free(context);return 1;}
    if(resources) {
        ok=output && asset_root && !wav && !audio_directory;
        if(ok) ok=resources_export(resources,asset_root,output,&error);
    } else if(wav || audio_directory) {
        if(!wav || !audio_directory || output || asset_root) ok=FALSE;
        else {
            char *path=audio_import_wav(wav,audio_directory,&error);ok=path!=NULL;
            if(path) g_print("WAV imported as VAG: %s\n",path);
            g_free(path);
        }
    } else {
        char *help=g_option_context_get_help(context,TRUE,NULL);g_print("%s",help);g_free(help);ok=FALSE;
    }
    if(!ok) g_printerr("PSX Lib: %s\n",error?error->message:"Invalid option combination.");
    g_clear_error(&error);g_option_context_free(context);
    g_free(output);g_free(asset_root);g_free(wav);g_free(audio_directory);g_free(resources);
    return ok?0:1;
}
