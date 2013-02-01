#include <stdio.h>
#include <stdlib.h>

#include <FreeImage.h>

#ifdef _WINDOWS
	#include <direct.h>
	#include "dirent-win.h"
#else
	#include <unistd.h>
	#include <dirent.h>
#endif

// Types
struct rect_t
{
	int x, y;
	int width, height;
};

struct texture_t
{
	char* filename;
	FIBITMAP* dib;
	int pixel_size;
	struct rect_t dest;
};

// Function declarations
struct texture_t load_texture(const char* filename);
void unload_texture(struct texture_t texture);
void sort_textures(struct texture_t* textures, int count);
BOOL save_atlas(const char* output_name, const char* output_text_name, int output_width, int output_height, FREE_IMAGE_FORMAT output_format, struct texture_t* textures, int count);
BOOL rect_overlaps(struct rect_t first, struct rect_t second);
void freeimage_error_handler(FREE_IMAGE_FORMAT fif, const char* message);

// Globals
char* lastError = NULL;

// Entry point
int main(int argc, char** argv)
{
	// Input variables
	char* input_dir = NULL;
	char* output_name = NULL;
	unsigned int min_width = 256;
	unsigned int min_height = 256;
	unsigned int max_width = 1024;
	unsigned int max_height = 1024;
	const FREE_IMAGE_FORMAT output_format = FIF_PNG;
	
	// Counters
	int i;
	int atlas_count;
	int current_texture;
	int start_texture;

	// State
	BOOL generated = FALSE;

	// Array of loaded textures
	int file_count = 0;
	int texture_count = 0;
	struct texture_t* textures = NULL;

	// Output texture information
	unsigned int output_width = 0;
	unsigned int output_height = 0;
	FIBITMAP* output_texture = NULL;
	
	// Directory entry
	DIR* dir;
	struct dirent* ent;
	char current_dir[1024];
	char* old_dir;

	// Parse command line options
	if (argc < 3)
	{
		printf("usage: %s inputdir outputfile [max_width max_height [min_width min_height]]\n", argv[0]);

		return 0;
	}

	input_dir = argv[1];
	output_name = argv[2];

	if (argc > 4)
	{
		max_width = atoi(argv[3]);
		max_height = atoi(argv[4]);

		if (argc > 6)
		{
			min_width = atoi(argv[5]);
			min_height = atoi(argv[6]);

			if (min_width <= 0 || min_height <= 0)
			{
				fprintf(stderr, "Invalid minimum dimensions %d, %d\n", min_width, min_height);

				return 0;
			}
		}
	}
	
	printf("Generating texture atlases from directory: %s\n", input_dir);
	printf("Output filename: %s\n", output_name);
	printf("Min dimensions: %d, %d\n", min_width, min_height);
	printf("Max dimensions: %d, %d\n\n", max_width, max_height);

	// Set FreeImage message handler
	FreeImage_SetOutputMessage(freeimage_error_handler);

	// Load all files from directory
	printf("Loading all textures from directory %s\n", input_dir);

	if ((dir = opendir(input_dir)) != NULL)
	{
		// Iterate through files to determine array length
		while ((ent = readdir(dir)) != NULL)
		{
			file_count++;
		}
		rewinddir(dir);

		// Allocate array
		textures = malloc(file_count * sizeof(struct texture_t));

		// Initialise array
		for (i = 0; i < file_count; ++i)
		{
			textures[i].dib = NULL;
			textures[i].filename = NULL;
		}

		// Change to texture directory
		// Headers are not #included for getcwd and chdir as they vary between environments
		old_dir = _getcwd(current_dir, 1024);
		_chdir(input_dir);
		
		// Load textures
		for (i = 0; i < file_count; ++i)
		{
			if ((ent = readdir(dir)) != NULL)
			{
				// Make sure it's an actual file
				if (strcmp(ent->d_name, ".") > 0 && strcmp(ent->d_name, "..") > 0)
				{
					// Attempt to load file as image
					textures[texture_count] = load_texture(ent->d_name);

					// If we successfully loaded an image, increment the count
					if (textures[texture_count].dib != NULL)
						texture_count++;
				}
			}
		}

		// Eliminate unloaded textures
		if (file_count > texture_count)
		{
			textures = realloc(textures, texture_count * sizeof(struct texture_t));
		}

		// Change directory back afterwards
		if (old_dir != NULL)
			_chdir(old_dir);

		// Close directory
		closedir(dir);
	}
	else
	{
		fprintf(stderr, "Failed to open directory %s\n", input_dir);

		goto cleanup;
	}

	// Generate atlas
	printf("Generating atlases from %d textures\n\n", texture_count);
	
	// Set output texture dimensions
	output_width = max_width;
	output_height = max_height;

	// Attempt to generate atlases
	atlas_count = 1;
	start_texture = 0;
	current_texture = start_texture;
	while (current_texture < texture_count)
	{
		unsigned int current_width, current_height;

		printf("Generating atlas %d\n", atlas_count);

		// Save starting texture for this atlas
		start_texture = current_texture;

		for (i = 0; i < texture_count; ++i)
		{
			unsigned int texture_width;
			unsigned int texture_height;

			texture_width = FreeImage_GetWidth(textures[i].dib);
			texture_height = FreeImage_GetHeight(textures[i].dib);

			if (texture_width > max_width || texture_height > max_height)
			{
				fprintf(stderr, "Texture %s (%d, %d) too big for atlas (%d, %d)\n",
						textures[i].filename, texture_width, texture_height, max_width, max_height);
				printf("Generation cannot continue\n");

				goto cleanup;
			}
		
			textures[i].dest.width = texture_width;
			textures[i].dest.height = texture_height;

			textures[i].pixel_size = texture_width * texture_height;
		}

		// Sort textures by pixel size
		sort_textures(textures, texture_count);

		// Start at min_height and min_width and double them until we can fit all textures into the atlas
		current_height = min_height;
		for (current_height = min_height; current_height <= max_height && !generated; current_height *= 2)
		{
			if (current_height > max_height)
				current_height = max_height;

			output_height = current_height;

			for (current_width = min_width; current_width <= max_width && !generated; current_width *= 2)
			{
				unsigned int x;
				unsigned int y;
				BOOL success;

				if (current_width > max_width)
					current_width = max_width;

				output_width = current_width;
				
				// Attempt to generate atlas of dimensions current_width * current_height
				printf("Attempting to generate atlas of dimensions %d, %d\n", current_width, current_height);

				// Initialise bounding rectangle x and ys
				for (i = 0; i < texture_count; ++i)
				{
					textures[i].dest.x = 0;
					textures[i].dest.y = 0;
				}

				// Fit each texture into atlas
				success = TRUE;
				for (current_texture = start_texture; current_texture < texture_count; ++current_texture)
				{
					BOOL fitted = FALSE;

					// Iterate through scanlines
					for (y = 0; y < current_height && !fitted; ++y)
					{
						if (y + textures[current_texture].dest.height > current_height)
							break;

						// Checking each (x, y) to see if the texture will fit here
						for (x = 0; x < current_width && !fitted; ++x)
						{
							BOOL overlap;

							if (x + textures[current_texture].dest.width > current_width)
								break;

							textures[current_texture].dest.x = x;
							textures[current_texture].dest.y = y;

							// Iterate through previous textures and check for overlap
							overlap = FALSE;
							for (i = 0; i < current_texture; ++i)
							{
								if (rect_overlaps(textures[current_texture].dest, textures[i].dest))
								{
									overlap = TRUE;
									break;
								}
							}

							if (!overlap)
							{
								fitted = TRUE;
							}
						}
					}

					if (!fitted)
					{
						success = FALSE;

						break;
					}
				}

				if (success)
				{
					generated = TRUE;
					break;
				}
			}

			if (generated)
			{
				break;
			}
		}

		// Save atlas (so far)
		printf("Atlas %d %s, saving atlas...\n\n", atlas_count, (generated ? "complete" : "full"));

		// Name this file output_filename + atlas_count
		if (atlas_count > 0)
		{
			// Append atlas count to filename before the file extension
			char* base_filename;
			char* atlas_filename;
			char* text_filename;
			char atlas_count_str[33];
			
			// Convert atlas count to string
			_itoa(atlas_count, atlas_count_str, 10);

			// Allocate memory for resultant filename string
			base_filename = malloc((strlen(output_name) + strlen(atlas_count_str) + 1) * sizeof(char));

			// Copy output filename to string
			strcpy(base_filename, output_name);
			
			// Replace file extension with atlas count
			strcpy(strstr(base_filename, "."), atlas_count_str);

			// Allocate strings for actual filenames
			atlas_filename = malloc((strlen(output_name) + strlen(atlas_count_str) + 1) * sizeof(char));
			text_filename = malloc((strlen(base_filename) + strlen(".txt") + 1) * sizeof(char));

			// Copy base filename
			strcpy(atlas_filename, base_filename);
			strcpy(text_filename, base_filename);

			// Append original file extension
			strcat(atlas_filename, strstr(output_name, "."));
			strcat(text_filename, ".txt");

			if (!save_atlas(atlas_filename, text_filename, output_width, output_height, output_format,
							textures + start_texture, current_texture - 1 - start_texture))
			{
				fprintf(stderr, "Unable to create atlas image %s\n", output_name);

				goto cleanup;
			}

			// Clean up memory
			free(base_filename);
			free(atlas_filename);
			free(text_filename);

			printf("\n");
		}

		// Increment atlas count
		atlas_count++;
	}

cleanup:
	// Clean up textures
	for (i = 0; i < texture_count; ++i)
		unload_texture(textures[i]);

	// Clean up bitmaps
	if (output_texture != NULL)
		FreeImage_Unload(output_texture);

	// Clean up memory
	free(textures);
	free(lastError);

	return 0;
}

// Loads a texture and returns a struct that includes a pointer to the device independent bitmap
struct texture_t load_texture(const char* filename)
{
	struct texture_t texture;
	FREE_IMAGE_FORMAT image_format;

	// Determine format of file
	image_format = FreeImage_GetFIFFromFilename(filename);

	if (image_format == FIF_UNKNOWN)
	{
		fprintf(stderr, "Failed to determine type of image %s\n", filename);

		texture.dib = NULL;
		texture.filename = NULL;
		return texture;
	}

	// Load image
	texture.dib = FreeImage_Load(image_format, filename, 0);

	if (texture.dib == NULL)
	{
		texture.filename = NULL;

		fprintf(stderr, "Unable to load image %s: %s\n", filename, lastError);
	}
	else
	{
		// Save filename of image
		texture.filename = malloc((strlen(filename) + 1) * sizeof(char));
		strcpy(texture.filename, filename);
	}

	return texture;
}

void unload_texture(struct texture_t texture)
{
	free(texture.filename);

	if (texture.dib != NULL)
		FreeImage_Unload(texture.dib);
}

void sort_textures(struct texture_t* textures, int count)
{
	int x, y;

	for (x = 0; x < count; ++x)
	{
		int min = x;
		struct texture_t temp;

		for (y = x; y < count; ++y)
		{
			if (textures[min].pixel_size < textures[y].pixel_size)
			{
				min = y;
			}
		}

		// Swap
		temp = textures[x];
		textures[x] = textures[min];
		textures[min] = temp;
	}
}

BOOL save_atlas(const char* output_name, const char* output_text_name, int output_width, int output_height, FREE_IMAGE_FORMAT output_format, struct texture_t* textures, int count)
{
	int i;
	FIBITMAP* output_texture;
	FILE* output_text;
	
	// Generate texture for atlas
	printf("Creating output texture for atlas %s\n", output_name);

	output_texture = FreeImage_Allocate(output_width, output_height, 32, 0, 0, 0);

	output_text = fopen(output_text_name, "w");

	for (i = 0; i < count; ++i)
	{
		fprintf(output_text, "\"%s\" %d %d\n", textures[i].filename, textures[i].dest.x, textures[i].dest.y);
		FreeImage_Paste(output_texture, textures[i].dib, textures[i].dest.x, textures[i].dest.y, 255);
	}

	// Write output texture
	printf("Writing texture atlas to %s\n", output_name);

	if (!FreeImage_Save(output_format, output_texture, output_name, 0))
	{
		fprintf(stderr, "Unable to save image %s\n", output_name);

		return FALSE;
	}

	FreeImage_Unload(output_texture);
	fclose(output_text);

	return TRUE;
}

BOOL rect_overlaps(struct rect_t first, struct rect_t second)
{
	// Check if rectangles overlap
	if (first.x > (second.x + second.width) || second.x > (first.x + first.width) ||
		first.y > (second.y + second.height) || second.y > (first.y + first.height))
	{
		return FALSE;
	}

	return TRUE;
}

void freeimage_error_handler(FREE_IMAGE_FORMAT fif, const char* message)
{
	free(lastError);

	lastError = malloc((strlen(message) + 1) * sizeof(char));
	strcpy(lastError, message);
}
