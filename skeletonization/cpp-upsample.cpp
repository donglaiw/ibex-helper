/* c++ file to upsample the skeletons to full resolution */

#include <unordered_set>
#include <map>
#include <queue>
#include <set>
#include "cpp-generate_skeletons.h"



// global variables for upsampling operation

static std::map<long, long> *down_to_up;
static long *segmentation;
static unsigned char *skeleton;
static std::set<std::pair<long, long> > connected_joints;



// convenient variables for moving between high and low resolutions

static float zdown;
static float ydown;
static float xdown;

static long up_grid_size[3];
static long up_nentries;
static long up_sheet_size;
static long up_row_size;

static long down_grid_size[3];
static long down_nentries;
static long down_sheet_size;
static long down_row_size;



// convenient variables for traversing arrays
static long offsets[26];
static void PopulateOffsets(void)
{
    offsets[0] = -1 * down_sheet_size - down_row_size - 1;
    offsets[1] = -1 * down_sheet_size - down_row_size;
    offsets[2] = -1 * down_sheet_size - down_row_size + 1;
    offsets[3] = -1 * down_sheet_size - 1;
    offsets[4] = -1 * down_sheet_size;
    offsets[5] = -1 * down_sheet_size + 1;
    offsets[6] = -1 * down_sheet_size + down_row_size - 1;
    offsets[7] = -1 * down_sheet_size + down_row_size;
    offsets[8] = -1 * down_sheet_size + down_row_size + 1;

    offsets[9] = -1 * down_row_size - 1;
    offsets[10] = -1 * down_row_size;
    offsets[11] = -1 * down_row_size + 1;
    offsets[12] = - 1;
    offsets[13] = + 1;
    offsets[14] = down_row_size - 1;
    offsets[15] = down_row_size;
    offsets[16] = down_row_size + 1;

    offsets[17] = down_sheet_size - down_row_size - 1;
    offsets[18] = down_sheet_size - down_row_size;
    offsets[19] = down_sheet_size - down_row_size + 1;
    offsets[20] = down_sheet_size - 1;
    offsets[21] = down_sheet_size;
    offsets[22] = down_sheet_size + 1;
    offsets[23] = down_sheet_size + down_row_size - 1;
    offsets[24] = down_sheet_size + down_row_size;
    offsets[25] = down_sheet_size + down_row_size + 1;
}





// struct definitions for finding any path between two joints

class AStarNode {
public:
    long iv;            // linear index
    long f;             // lower estimate of path through this node
    long g;             // true value from start node to this node
    long h;             // lower estimate of distance to target node

    AStarNode(long iv, long f, long g, long h) : iv(iv), f(f), g(g), h(g) {}
};



// flip the sign to work with priority queue
static bool operator<(const AStarNode &one, const AStarNode &two) {
    return one.f > two.f;
}



static long hscore(long index, long target_index)
{
    long iz = index / up_sheet_size;
    long iy = (index - iz * up_sheet_size) / up_row_size;
    long ix = index % up_row_size;

    long ik = target_index / up_sheet_size;
    long ij = (target_index - ik * up_sheet_size) / up_row_size;
    long ii = target_index % up_row_size;

    return (iz - ik) * (iz - ik) + (iy - ij) * (iy - ij) + (ix - ii) * (ix - ii);
}



// find if a path exists between a source and target node
static bool HasConnectedPath(long label, long source_index, long target_index)
{
    // upsample the source and target indices
    source_index = down_to_up[label][source_index];
    target_index = down_to_up[label][target_index];

    // don't allow the path to be max_expansion times the first h
    static const double max_expansion = 2;
    long h = hscore(source_index, target_index);
    long max_distance = max_expansion * h;

    // add the source to the node list
    AStarNode source_node = AStarNode(source_index, h, 0, h);

    // priority queue for A* Search
    std::priority_queue<AStarNode> open_queue;
    open_queue.push(source_node);

    // create a set of opened and closed nodes which should not be considered
    std::map<long, long> open_list;
    open_list[source_node.iv] = source_node.g;          // the mapping keeps track of best g value so far
    std::set<long> closed_list;

    while (!open_queue.empty()) {
        // pop the current best node from the list
        AStarNode current = open_queue.top(); open_queue.pop();

        // if this node is the target we win!
        if (current.iv == target_index) return true;
        if (current.g != open_list[current.iv]) continue;
        if (current.f > max_distance) break;

        // this node is now expanded
        closed_list.insert(current.iv);

        // get the cartesian indices
        long iz = current.iv / up_sheet_size;
        long iy = (current.iv - iz * up_sheet_size) / up_row_size;
        long ix = current.iv % up_row_size;

        for (long iw = -1; iw <= 1; ++iw) {
            for (long iv = -1; iv <= 1; ++iv) {
                for (long iu = -1; iu <= 1; ++iu) {
                    long ik = iz + iw;
                    long ij = iy + iv;
                    long ii = ix + iu;

                    long successor = ik * up_sheet_size + ij * up_row_size + ii;
                    if (successor < 0 or successor > up_nentries - 1) continue;
                    if (segmentation[successor] != label) continue;

                    // skip if already closed
                    if (closed_list.find(successor) != closed_list.end()) continue;

                    long successor_g = current.g + iw * iw + iv * iv + iu * iu;
                    if (!open_list[successor]) {
                        long g = successor_g;
                        long h = hscore(successor, target_index);
                        long f = g + h;
                        AStarNode successor_node = AStarNode(successor, f, g, h);
                        open_queue.push(successor_node);
                        open_list[successor] = g;
                    }
                    // this is not the best path to this node
                    else if (successor_g >= open_list[successor]) continue;
                    // update the value (keep the old one on the queue)
                    else {
                        long g = successor_g;
                        long h = hscore(successor, target_index);
                        long f = g + h;
                        AStarNode successor_node = AStarNode(successor, f, g, h);
                        open_queue.push(successor_node);
                        open_list[successor] = g;
                    }
                }
            }
        }
    }

    return false;
}



static bool IsEndpoint(long source_index, long label)
{
    short nneighbors = 0;
    for (long iv = 0; iv < 26; ++iv) {
        long target_index = source_index + offsets[iv];
        if (target_index < 0 or target_index > down_nentries - 1) continue;
        if (!skeleton[target_index]) continue;
        
        std::pair<long, long> query;
        if (source_index < target_index) query = std::pair<long, long>(source_index, target_index);
        else query = std::pair<long, long>(target_index, source_index);

        if (connected_joints.find(query) != connected_joints.end()) nneighbors++;
    }

    // return if there is one neighbor (other than iv) that is 1
    if (nneighbors < 2) return true;
    else return false;
}



static int MapDown2Up(const char *prefix, long skeleton_resolution[3], bool benchmark)
{
    // get the downsample filename
    char downsample_filename[4096];
    if (benchmark) sprintf(downsample_filename, "benchmarks/skeleton/%s-downsample-%ldx%ldx%ld.bytes", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z]);
    else sprintf(downsample_filename, "skeletons/%s/downsample-%ldx%ldx%ld.bytes", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z]);

    FILE *dfp = fopen(downsample_filename, "rb"); 
    if (!dfp) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }

    // get the upsample filename
    char upsample_filename[4096];
    if (benchmark) sprintf(upsample_filename, "benchmarks/skeleton/%s-upsample-%ldx%ldx%ld.bytes", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z]);
    else sprintf(upsample_filename, "skeletons/%s/upsample-%ldx%ldx%ld.bytes", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z]);

    FILE *ufp = fopen(upsample_filename, "rb");
    if (!ufp) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }

    // read downsample header
    long down_max_segment;
    if (fread(&(down_grid_size[IB_Z]), sizeof(long), 1, dfp) != 1) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }
    if (fread(&(down_grid_size[IB_Y]), sizeof(long), 1, dfp) != 1) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }
    if (fread(&(down_grid_size[IB_X]), sizeof(long), 1, dfp) != 1) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }
    if (fread(&down_max_segment, sizeof(long), 1, dfp) != 1) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }

    // read upsample header
    long up_max_segment;
    if (fread(&(up_grid_size[IB_Z]), sizeof(long), 1, ufp) != 1) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }
    if (fread(&(up_grid_size[IB_Y]), sizeof(long), 1, ufp) != 1) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }
    if (fread(&(up_grid_size[IB_X]), sizeof(long), 1, ufp) != 1) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }
    if (fread(&up_max_segment, sizeof(long), 1, ufp) != 1) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }

    down_to_up = new std::map<long, long>[up_max_segment];
    for (long label = 0; label < up_max_segment; ++label) {
        down_to_up[label] = std::map<long, long>();

        long down_nelements, up_nelements;
        if (fread(&down_nelements, sizeof(long), 1, dfp) != 1) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }
        if (fread(&up_nelements, sizeof(long), 1, ufp) != 1) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }

        long *down_elements = new long[down_nelements];
        long *up_elements = new long[up_nelements];
        if (fread(down_elements, sizeof(long), down_nelements, dfp) != (unsigned long)down_nelements) { fprintf(stderr, "Failed to read %s\n", downsample_filename); return 0; }
        if (fread(up_elements, sizeof(long), up_nelements, ufp) != (unsigned long)up_nelements) { fprintf(stderr, "Failed to read %s\n", upsample_filename); return 0; }

        for (long ie = 0; ie < down_nelements; ++ie)
            down_to_up[label][down_elements[ie]] = up_elements[ie];
    }

    fclose(dfp);
    fclose(ufp);

    return 1;
}



// operation that takes skeletons and 
void CppApplyUpsampleOperation(const char *prefix, long *input_segmentation, long skeleton_resolution[3], long output_resolution[3], const char *skeleton_algorithm, bool benchmark)
{
    // get the mapping from downsampled locations to upsampled ones
    if (!MapDown2Up(prefix, skeleton_resolution, benchmark)) return;

    // get a list of labels for each downsampled index
    segmentation = input_segmentation;

    // get downsample ratios
    zdown = ((float) skeleton_resolution[IB_Z]) / output_resolution[IB_Z];
    ydown = ((float) skeleton_resolution[IB_Y]) / output_resolution[IB_Y];
    xdown = ((float) skeleton_resolution[IB_X]) / output_resolution[IB_X];

    // set global variables
    up_nentries = up_grid_size[IB_Z] * up_grid_size[IB_Y] * up_grid_size[IB_X];
    up_sheet_size = up_grid_size[IB_Y] * up_grid_size[IB_X];
    up_row_size = up_grid_size[IB_X];

    down_nentries = down_grid_size[IB_Z] * down_grid_size[IB_Y] * down_grid_size[IB_X];
    down_sheet_size = down_grid_size[IB_Y] * down_grid_size[IB_X];
    down_row_size = down_grid_size[IB_X];

    // create offset array
    PopulateOffsets();


    // I/O filenames
    char input_filename[4096];
    if (benchmark) sprintf(input_filename, "benchmarks/skeleton/%s-downsample-%ldx%ldx%ld-%s-skeleton.pts", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z], skeleton_algorithm);
    else sprintf(input_filename, "skeletons/%s/downsample-%ldx%ldx%ld-%s-skeleton.pts", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z], skeleton_algorithm);

    char output_filename[4096];
    if (benchmark) sprintf(output_filename, "benchmarks/skeleton/%s-%ldx%ldx%ld-%s-skeleton.pts", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z], skeleton_algorithm);
    else sprintf(output_filename, "skeletons/%s/%ldx%ldx%ld-%s-skeleton.pts", prefix, skeleton_resolution[IB_X], skeleton_resolution[IB_Y], skeleton_resolution[IB_Z], skeleton_algorithm);

    // open files for read/write
    FILE *rfp = fopen(input_filename, "rb");
    if (!rfp) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }

    FILE *wfp = fopen(output_filename, "wb");
    if (!wfp) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }

    // read header
    long max_label;
    long input_grid_size[3];
    if (fread(&(input_grid_size[IB_Z]), sizeof(long), 1, rfp) != 1) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
    if (fread(&(input_grid_size[IB_Y]), sizeof(long), 1, rfp) != 1) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
    if (fread(&(input_grid_size[IB_X]), sizeof(long), 1, rfp) != 1) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
    if (fread(&max_label, sizeof(long), 1, rfp) != 1) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
    
    // write the header
    if (fwrite(&(up_grid_size[IB_Z]), sizeof(long), 1, wfp) != 1) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }
    if (fwrite(&(up_grid_size[IB_Y]), sizeof(long), 1, wfp) != 1) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }
    if (fwrite(&(up_grid_size[IB_X]), sizeof(long), 1, wfp) != 1) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }
    if (fwrite(&max_label, sizeof(long), 1, wfp) != 1) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }



    // go through all skeletons
    for (long label = 0; label < max_label; ++label) {
        long nelements;
        if (fread(&nelements, sizeof(long), 1, rfp) != 1) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
        if (fwrite(&nelements, sizeof(long), 1, wfp) != 1) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }

        //create an empty array for this skeleton
        skeleton = new unsigned char[down_nentries];
        for (long iv = 0; iv < down_nentries; ++iv) skeleton[iv] = 0;

        // find all of the downsampled elements
        long *down_elements = new long[nelements];
        if (fread(down_elements, sizeof(long), nelements, rfp) != (unsigned long)nelements) { fprintf(stderr, "Failed to read %s\n", input_filename); return; }
        for (long ie = 0; ie < nelements; ++ie) skeleton[down_elements[ie]] = 1;

        // find all skeleton pairs that need to be checked as neighbors
        for (long ie = 0; ie < nelements; ++ie) {
            long source_index = down_elements[ie];
            for (long iv = 13; iv < 26; ++iv) {
                long target_index = source_index + offsets[iv];
                if (target_index > down_nentries - 1) continue;
                if (!skeleton[target_index]) continue;

                if (HasConnectedPath(label, source_index, target_index)) {
                    connected_joints.insert(std::pair<long, long>(source_index, target_index));
                }
            }
        }

        // find the upsampled elements
        long *up_elements = new long[nelements];
        for (long ie = 0; ie < nelements; ++ie) {
            long down_index = down_elements[ie];

            // see if this skeleton location is actually an endpoint
            up_elements[ie] = down_to_up[label][down_index];
            if (IsEndpoint(down_index, label)) up_elements[ie] = -1 * up_elements[ie];
        }

        if (fwrite(up_elements, sizeof(long), nelements, wfp) != (unsigned long)nelements) { fprintf(stderr, "Failed to write %s\n", output_filename); return; }

        // clear the set of connected joints
        connected_joints.clear();

        // free memory
        delete[] skeleton;
        delete[] down_elements;
        delete[] up_elements;
    }

    // free memory
    delete[] down_to_up;

    // close the files
    fclose(rfp);
    fclose(wfp);
}