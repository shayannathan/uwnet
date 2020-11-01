#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#include "uwnet.h"

// Add bias terms to a matrix
// matrix xw: partially computed output of layer
// matrix b: bias to add in (should only be one row!)
// returns: y = wx + b
matrix forward_convolutional_bias(matrix xw, matrix b)
{
    assert(b.rows == 1);
    assert(xw.cols % b.cols == 0);

    matrix y = copy_matrix(xw);
    int spatial = xw.cols / b.cols;
    int i,j;
    for(i = 0; i < y.rows; ++i){
        for(j = 0; j < y.cols; ++j){
            y.data[i*y.cols + j] += b.data[j/spatial];
        }
    }
    return y;
}

// Calculate dL/db from a dL/dy
// matrix dy: derivative of loss wrt xw+b, dL/d(xw+b)
// returns: derivative of loss wrt b, dL/db
matrix backward_convolutional_bias(matrix dy, int n)
{
    assert(dy.cols % n == 0);
    matrix db = make_matrix(1, n);
    int spatial = dy.cols / n;
    int i,j;
    for(i = 0; i < dy.rows; ++i){
        for(j = 0; j < dy.cols; ++j){
            db.data[j/spatial] += dy.data[i*dy.cols + j];
        }
    }
    return db;
}

//Helper Function to get needed pixel from input or 0 if in padding
float get_pixel_value(image im, int row, int col, int channel, int pad)
{
    row -= pad;
    col -= pad;

    if (row < 0 || col < 0 || row >= im.h || col >= im.w) {
        return 0;
    }
    return im.data[col + im.w*(row + im.h*channel)];
}

// Make a column matrix out of an image
// image im: image to process
// int size: kernel size for convolution operation
// int stride: stride for convolution
// returns: column matrix
matrix im2col(image im, int size, int stride)
{
    int i, j, k, paddingSize; //i = row, j = column, k = channel
    int outw = (im.w-1)/stride + 1; //Adds 1 to account for integer division
    int outh = (im.h-1)/stride + 1; //Number of elements we look at for row and column
    int rows = im.c*size*size;
    int cols = outw * outh; //Total num of pixels we find convolution for after stride
    matrix col = make_matrix(rows, cols);

    // TODO: 5.1
    // Fill in the column matrix with patches from the image

    //1. For each element, we need to get the size x size square of neighbors as each column
    //2. If we look at boundary elements, we need to add 0 padding if kernel goes off input matrix
    //3. Each column is actually (size x size)* # of channels, because we must do this process for each channel

    if(size % 2 == 0) { //Even
        paddingSize = 0;
    } else { //Odd
        paddingSize = size/2;
    }
    for (k = 0; k < rows; ++k) {
        //Offset of width in kernel (should change 0 to n-1 for each of the n rows)
        int w_offset = k % size;

        //Offset of height in kernel (should change 0 to n-1 for whole kernel)
        int h_offset = (k / size) % size;

        // (size / size) = # elements in the kernel. c_im is channel we're looking at
        int c_im = k / size / size;

        //Looking At Input to get value for output. We run kernel over input and get value of offset
        for (i = 0; i < outh; ++i) {
            for (j = 0; j < outw; ++j) {
                int im_row = h_offset + i * stride;
                int im_col = w_offset + j * stride;
                int col_index = (k * outh + i) * outw + j;
                col.data[col_index] = get_pixel_value(im, im_row, im_col, c_im, paddingSize);
            }
        }

        /*
        TEST FAILING FOR EVEN SIZED KERNEL. HOW DO WE CENTER IT?
        Error: differs at 0, 0.180392 vs 0.000000 so first element is getting buffered
        */
    }

    return col;
}

//Helper Function to set pixel
void set_pixel_value(image im, int row, int col, int channel, int pad, float val)
{
    row -= pad;
    col -= pad;

    if (row >= 0 && col >= 0 && row < im.h && col < im.w) {
        im.data[col + im.w*(row + im.h*channel)] += val;
    }
}

// The reverse of im2col, add elements back into image
// matrix col: column matrix to put back into image
// int size: kernel size
// int stride: convolution stride
// image im: image to add elements back into
image col2im(int width, int height, int channels, matrix col, int size, int stride)
{
    int i, j, k, paddingSize;

    image im = make_image(width, height, channels);
    //int outw = (im.w-1)/stride + 1;
    int rows = channels*size*size;

    // TODO: 5.2
    // Add values into image im from the column matrix
    if(size % 2 == 0) { //Even
        paddingSize = 0;
    } else { //Odd
        paddingSize = size/2;
    }
    int outh = (height + 2 * paddingSize - size) / stride + 1;
    int outw = (width + 2 * paddingSize - size) / stride + 1;

    for (k = 0; k < rows; ++k) {
        int w_offset = k % size;
        int h_offset = (k / size) % size;
        int c_im = k / size / size;
        for (i = 0; i < outh; ++i) {
            for (j = 0; j < outw; ++j) {
                int im_row = h_offset + i * stride;
                int im_col = w_offset + j * stride;
                int col_index = (k * outh + i) * outw + j;
                float val = col.data[col_index];
                set_pixel_value(im, im_row, im_col, c_im, paddingSize, val);
            }
        }
    }

    return im;

    /*
    FAILING FOR SIMILAR REASON AS im2col WITH EVEN SIZED KERNEL
    */
}

// Run a convolutional layer on input
// layer l: pointer to layer to run
// matrix in: input to layer
// returns: the result of running the layer
matrix forward_convolutional_layer(layer l, matrix in)
{
    assert(in.cols == l.width*l.height*l.channels);
    // Saving our input
    // Probably don't change this
    free_matrix(*l.x);
    *l.x = copy_matrix(in);

    int i, j;
    int outw = (l.width-1)/l.stride + 1;
    int outh = (l.height-1)/l.stride + 1;
    matrix out = make_matrix(in.rows, outw*outh*l.filters);
    for(i = 0; i < in.rows; ++i){
        image example = float_to_image(in.data + i*in.cols, l.width, l.height, l.channels);
        matrix x = im2col(example, l.size, l.stride);
        matrix wx = matmul(l.w, x);
        for(j = 0; j < wx.rows*wx.cols; ++j){
            out.data[i*out.cols + j] = wx.data[j];
        }
        free_matrix(x);
        free_matrix(wx);
    }
    matrix y = forward_convolutional_bias(out, l.b);
    free_matrix(out);

    return y;
}

// Run a convolutional layer backward
// layer l: layer to run
// matrix dy: dL/dy for this layer
// returns: dL/dx for this layer
matrix backward_convolutional_layer(layer l, matrix dy)
{
    matrix in = *l.x;
    assert(in.cols == l.width*l.height*l.channels);

    int i;
    int outw = (l.width-1)/l.stride + 1;
    int outh = (l.height-1)/l.stride + 1;


    matrix db = backward_convolutional_bias(dy, l.db.cols);
    axpy_matrix(1, db, l.db);
    free_matrix(db);


    matrix dx = make_matrix(dy.rows, l.width*l.height*l.channels);
    matrix wt = transpose_matrix(l.w);

    for(i = 0; i < in.rows; ++i){
        image example = float_to_image(in.data + i*in.cols, l.width, l.height, l.channels);

        dy.rows = l.filters;
        dy.cols = outw*outh;

        matrix x = im2col(example, l.size, l.stride);
        matrix xt = transpose_matrix(x);
        matrix dw = matmul(dy, xt);
        axpy_matrix(1, dw, l.dw);

        matrix col = matmul(wt, dy);
        image dxi = col2im(l.width, l.height, l.channels, col, l.size, l.stride);
        memcpy(dx.data + i*dx.cols, dxi.data, dx.cols * sizeof(float));
        free_matrix(col);

        free_matrix(x);
        free_matrix(xt);
        free_matrix(dw);
        free_image(dxi);

        dy.data = dy.data + dy.rows*dy.cols;
    }
    free_matrix(wt);
    return dx;

}

// Update convolutional layer
// layer l: layer to update
// float rate: learning rate
// float momentum: momentum term
// float decay: l2 regularization term
void update_convolutional_layer(layer l, float rate, float momentum, float decay)
{

    // TODO: 5.3
    // Apply our updates using our SGD update rule
    // assume  l.dw = dL/dw - momentum * update_prev
    // we want l.dw = dL/dw - momentum * update_prev + decay * w
    // then we update l.w = l.w - rate * l.dw
    // lastly, l.dw is the negative update (-update) but for the next iteration
    // we want it to be (-momentum * update) so we just need to scale it a little

    axpy_matrix(decay, l.w, l.dw);
    axpy_matrix(-rate, l.dw, l.w);
    scal_matrix(momentum, l.dw);

    // Do the same for biases as well but no need to use weight decay on biases

    axpy_matrix(-rate, l.db, l.b);
    scal_matrix(momentum, l.db);
}

// Make a new convolutional layer
// int w: width of input image
// int h: height of input image
// int c: number of channels
// int size: size of convolutional filter to apply
// int stride: stride of operation
layer make_convolutional_layer(int w, int h, int c, int filters, int size, int stride)
{
    layer l = {0};
    l.width = w;
    l.height = h;
    l.channels = c;
    l.filters = filters;
    l.size = size;
    l.stride = stride;
    l.w  = random_matrix(filters, size*size*c, sqrtf(2.f/(size*size*c)));
    l.dw = make_matrix(filters, size*size*c);
    l.b  = make_matrix(1, filters);
    l.db = make_matrix(1, filters);
    l.x = calloc(1, sizeof(matrix));
    l.forward  = forward_convolutional_layer;
    l.backward = backward_convolutional_layer;
    l.update   = update_convolutional_layer;
    return l;
}

