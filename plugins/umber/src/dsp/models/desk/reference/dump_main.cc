// dump: rate f A seconds mode(0 chain,1 IT,2 OT) -> binary doubles: y, H, B per sample
#include "dsp/models/desk/DeskModel.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
using namespace umber::dsp;
int main (int argc, char** argv)
{
    double rate = atof (argv[1]), f = atof (argv[2]), A = atof (argv[3]), sec = atof (argv[4]); int mode = atoi (argv[5]);
    auto c = DeskCircuit::standard();
    desk::TransformerStage it, ot; desk::LineAmp amp;
    it.setParams (c.input); ot.setParams (c.output); amp.setParams (c.amp);
    it.prepare (rate); ot.prepare (rate); amp.prepare (rate);
    int n = (int) (sec * rate);
    std::vector<double> out ((size_t) n * 3);
    for (int i = 0; i < n; ++i)
    {
        double x = A * std::sin (2 * M_PI * f * i / rate);
        double y; const desk::TransformerStage* t;
        if (mode == 1) { y = it.process (c.voltsPerUnit * x); t = &it; }
        else if (mode == 2) { y = ot.process (15.0 * x); t = &ot; }
        else { y = ot.process (amp.staticTransfer (it.process (c.voltsPerUnit * x))); t = &ot; }
        out[3*i] = y; out[3*i+1] = t->fieldH(); out[3*i+2] = t->fluxDensity();
    }
    fwrite (out.data(), sizeof (double), out.size(), stdout);
}
