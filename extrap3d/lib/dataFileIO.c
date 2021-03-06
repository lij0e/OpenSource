#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include "par.h"
#include "segy.h"
#include "Area.h"

void cr1fft(complex *cdata, float *rdata, int n, int sign);
void rc1fft(float *rdata, complex *cdata, int n, int sign);

static int binary_file;

int read_FFT_DataFile(FILE *fp, complex *data, Area vel_area, int nfft, int nw, int nw_low, int *tr_read_in, int *tr_shot, int *ixmin, int *ixmax, int *iymin, int *iymax, int *sx, int *sy, int conjg, int verbose)
{
	int ix, iy, iw, i, nt, nx, ny, sxy, nfreq, pos, sign;
	float xvmin, yvmin;
	int err=0;
	int one_shot, traces_shot, fldr_s, traces_read_in;
	size_t  nread;
	float dx, dy, *trace, scl;
	complex *ctrace;
	segy *hdr;

	nx  = vel_area.nx;
	ny  = vel_area.ny;
	sxy = vel_area.sxy;
	dx  = vel_area.dx;
	dy  = vel_area.dy;
	xvmin = vel_area.xmin;
	yvmin = vel_area.ymin;

	sign   = -1;
	nfreq  = nfft/2 + 1;
	hdr    = (segy *)malloc(TRCBYTES);
    trace  =   (float *)calloc(nfft,sizeof(float));
    ctrace = (complex *)malloc(nfreq*sizeof(complex));

	one_shot=1;
	traces_shot = 0;
	traces_read_in = *tr_read_in;
	while (one_shot) {
		nread = fread( hdr, 1, TRCBYTES, fp );
		if (nread==0) {
			err = -1;
			break;
		}
		nt = hdr[0].ns;
		if (traces_shot==0) fldr_s = hdr[0].fldr;
		if ((fldr_s != hdr[0].fldr)) {
			fseek(fp, -TRCBYTES, SEEK_CUR);
			break;
		}

		if (traces_shot==0) {
			if (hdr[0].scalco < 0) scl = 1.0/fabs(hdr[0].scalco);
			else if (hdr[0].scalco == 0) scl = 1.0;
			else scl = hdr[0].scalco;
			*sx = hdr[0].sx;
			*sy = hdr[0].sy;
		}

		nread = fread( trace, sizeof(float), nt, fp );
		assert (nread == nt);
		traces_shot++;
		traces_read_in++;
	
		if (nfft > nt) memset( &trace[nt], 0, sizeof(float)*(nfft-nt) ); 
		rc1fft(trace,ctrace,nfft,sign);

		ix = (hdr[0].gx*scl-xvmin)/dx;
		iy = (hdr[0].gy*scl-yvmin)/dy;
		if (ix >=0 && ix<nx && iy>=0 && iy<ny) {
			for (iw=0; iw<nw; iw++) {
				data[iw*sxy+iy*nx+ix].r = ctrace[nw_low+iw].r;
				data[iw*sxy+iy*nx+ix].i = conjg*ctrace[nw_low+iw].i;
			}
			*ixmin = MIN(ix,*ixmin);
			*iymin = MIN(iy,*iymin);
			*ixmax = MAX(ix,*ixmax);
			*iymax = MAX(iy,*iymax);
		}
		else {
			fprintf(stderr,"*** trace at %.2f (%d), %.2f (%d) outside model\n",
				hdr[0].gx*scl, ix, hdr[0].gy*scl, iy);
		}

		if (verbose>2) {
			fprintf(stderr," trace %d: gx = %.2f(%d) gy = %.2f(%d) \n", 
				traces_shot, hdr[0].gx*scl, ix, hdr[0].gy*scl, iy);
		}

	} /* end of receiver gather */
	*tr_read_in = traces_read_in;
	*tr_shot    = traces_shot;

	free(hdr);
	free(trace);
	free(ctrace);

	return err;
}


int write_FFT_DataFile(FILE *fp, complex *data, Area data_area, int fldr, int nt, int nfft, int nw, int nw_low, float dt, int out_su, int conjg, int verbose)
{
	int ix, iy, iw, i, nx, ny, sxy, nfreq, pos, sign;
	int xvmin, yvmin;
	size_t  nwrite;
	float dx, dy, *trace;
	complex *ctrace;
	segy *hdr;

	nx  = data_area.nx;
	ny  = data_area.ny;
	sxy = data_area.sxy;
	dx  = data_area.dx;
	dy  = data_area.dy;
	xvmin = data_area.xmin;
	yvmin = data_area.ymin;
	nfreq = nfft/2 + 1;
    sign  = 1;

    trace  =   (float *)calloc(nfft,sizeof(float));
    ctrace = (complex *)malloc(nfreq*sizeof(complex));
	hdr    = (segy *)calloc(1,TRCBYTES);

	hdr[0].trid = 1;
	hdr[0].fldr = fldr;
	hdr[0].trwf = nx;
	hdr[0].ntr = nx*ny;
	hdr[0].ns = nt;
	hdr[0].dt = 1000000*dt;
	hdr[0].f1 = 0.0;
	hdr[0].f2 = xvmin;
	hdr[0].d1 = dt;
	hdr[0].d2 = dx;
    hdr[0].scalco = -1000;

    for (iy=0; iy<ny; iy++) {
        for (ix=0; ix<nx; ix++) {
			pos = iy*nx+ix;
			for (i=0; i<nfreq; i++) 
				ctrace[i].r = ctrace[i].i = 0.0;
			for (iw=0; iw<nw; iw++) {
				ctrace[iw+nw_low].r = data[iw*sxy+pos].r;
				ctrace[iw+nw_low].i = conjg*data[iw*sxy+pos].i;
			}

            /* transform back to time */
            cr1fft(ctrace, trace, nfft, sign);

            /* write to output file */
             if (out_su) {
                hdr[0].gx = (int)(xvmin+ix*dx)*1000;
                hdr[0].gy = (int)(yvmin+iy*dy)*1000;
		        hdr[0].f2 = xvmin+ix*dx;
		        hdr[0].tracf = ix+1;
		        hdr[0].tracl = iy*nx+ix+1;
		        nwrite = fwrite(hdr, 1, TRCBYTES, fp);
		        assert( nwrite == TRCBYTES );
		        nwrite = fwrite(trace, sizeof(float), nt, fp);
		        assert( nwrite == nt );
            }
            else {
	            nwrite = fwrite(trace, sizeof(float), nt, fp);
	            assert( nwrite == nt );
            }
        }
    }
	fflush(fp);

	free(trace);
	free(ctrace);
	free(hdr);

	return 0;
}


int write_ImageFile(FILE *fp, float *data, Area data_area, int fldr, int d, int out_su, int verbose)
{
	size_t nwrite;
	int ix, iy, j, ny, nx, nz, nxy;
	int xvmin, yvmin;
	float dx, dy, dz;
	segy *hdr;

	nx  = data_area.nx;
	ny  = data_area.ny;
	nz  = data_area.nz;
	nxy = data_area.sxy;
	dx  = data_area.dx;
	dy  = data_area.dy;
	dz  = data_area.dz;
	xvmin = data_area.xmin;
	yvmin = data_area.ymin;

	/* write image file is same order as the velocity file */

	hdr    = (segy *)calloc(1,TRCBYTES);
	hdr[0].ns = nx;
	hdr[0].dt = dz;
	hdr[0].trwf = ny; 
	hdr[0].ntr = ny*nz;
	hdr[0].f1 = xvmin; 
	hdr[0].f2 = yvmin;
	hdr[0].d1 = dx;
	hdr[0].d2 = dy;
	hdr[0].gx =(int)xvmin;
	hdr[0].scalco = -1000;
	hdr[0].trid = 30;
	hdr[0].fldr = fldr;
	hdr[0].sdepth = data_area.zmin+d*dz;

	if (out_su) {
		for (iy=0; iy<ny; iy++) {
			hdr[0].gy = (int)(yvmin+iy*dy)*1000;
			hdr[0].f2 = yvmin+iy*dy;
			hdr[0].tracf = iy+1;
			nwrite = fwrite(hdr, 1, TRCBYTES, fp);
			assert( nwrite == TRCBYTES );

			nwrite = fwrite(&data[iy*nx], sizeof(float), nx, fp);
			assert( nwrite == nx );
		}
	}
	else {
		nwrite = fwrite(data, sizeof(float), nxy, fp);
		assert( nwrite == nxy );
	}
	fflush(fp);

	free(hdr);

	return 0;

}
