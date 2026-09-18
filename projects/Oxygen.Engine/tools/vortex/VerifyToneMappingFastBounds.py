"""Independent arithmetic allowance derivation for the quick SDR enclosure."""

from decimal import Decimal, localcontext
from fractions import Fraction as F
from pathlib import Path
import argparse
import json
import struct


def f32(value):
    return F(struct.unpack('<f', struct.pack('<f', value))[0])


def derive():
    u, reciprocal, ftz = F(1, 2**23), F(1, 2**21), F(1, 2**126)
    def constant(value):
        return abs(f32(value)), F(0)
    def add(a, b):
        magnitude, inherited = a[0]+b[0], a[1]+b[1]
        return magnitude, inherited + u*(magnitude+inherited) + ftz
    def multiply(a, b):
        magnitude = a[0]*b[0]
        inherited = a[0]*b[1] + b[0]*a[1] + a[1]*b[1]
        return magnitude, inherited + u*(magnitude+inherited) + ftz
    # s=x/max(x,1), q=1/max(x,1); s,q <=1 and at least one equals1.
    scaled = F(1), reciprocal + u*(1+reciprocal) + ftz
    inverse = F(1), reciprocal
    numerator = add(multiply(scaled, add(scaled, multiply(constant(.0245786), inverse))),
                    multiply(multiply(constant(.000090537), inverse), inverse))
    denominator = add(multiply(scaled, add(multiply(constant(.983729), scaled),
                                          multiply(constant(.432951), inverse))),
                      multiply(multiply(constant(.238081), inverse), inverse))
    # Coefficientwise nonnegativity proves -.001 <= a/b <=1.02.
    assert F(102,100)*f32(.983729) >= 1
    assert F(102,100)*f32(.432951) >= f32(.0245786)
    assert F(1,1000)*f32(.238081) >= f32(.000090537)
    # Positive derivative coefficients establish the endpoint enclosure.
    assert f32(.432951)-f32(.0245786)*f32(.983729) > 0
    assert f32(.238081)+f32(.000090537)*f32(.983729) > 0
    assert f32(.0245786)*f32(.238081)+f32(.000090537)*f32(.432951) > 0
    curve_error = (numerator[1]+F(102,100)*denominator[1]) / (f32(.238081)-denominator[1])
    curve_error += (reciprocal+u*(1+reciprocal))*(F(102,100)+curve_error)+ftz
    assert 2*curve_error < F(1, 2**14)
    filmic_numerator = add(multiply(scaled, add(multiply(constant(.15), scaled),
        multiply(multiply(constant(.1), constant(.5)), inverse))),
        multiply(multiply(multiply(constant(.2), constant(.02)), inverse), inverse))
    filmic_denominator = add(multiply(scaled, add(multiply(constant(.15), scaled),
        multiply(constant(.5), inverse))),
        multiply(multiply(multiply(constant(.2), constant(.3)), inverse), inverse))
    filmic_error = (filmic_numerator[1]+filmic_denominator[1]) / (f32(.2)*f32(.3)-filmic_denominator[1])
    division_error = reciprocal+u*(1+reciprocal)
    filmic_error += division_error*(1+filmic_error)+ftz
    constant_error = division_error*f32(.02)/f32(.3)
    filmic_error += constant_error+u*(1+filmic_error+f32(.02)/f32(.3)+constant_error)+ftz
    x=f32(11.2)
    white = (x*(f32(.15)*x+f32(.1)*f32(.5))+f32(.2)*f32(.02))/(x*(f32(.15)*x+f32(.5))+f32(.2)*f32(.3))-f32(.02)/f32(.3)
    assert white > F(7,10)
    assert (1-f32(.02)/f32(.3))/F(7,10) < F(134,100)
    # Its derivative has positive coefficients for x>=0.
    assert f32(.3)*f32(.1)>f32(.02)
    normalized_filmic_error = F(234,100)*filmic_error/(F(7,10)-filmic_error)
    normalized_filmic_error += division_error*(F(134,100)+normalized_filmic_error)+ftz
    assert 2*normalized_filmic_error < F(1,2**12)
    output_matrix_l1 = sum(abs(f32(x)) for x in (1.60475, -.53108, -.07367))
    matrix_error = 2*(11*u/(1-11*u))*output_matrix_l1*F(102,100)
    assert matrix_error < F(1, 2**17)
    # pow(x,1/gamma), 0<=x<=2 and 1<=gamma<=4. The log error law is
    # absolute near one and relative elsewhere. Bound delta exponent by
    # a*abs(q)+b, then maximize abs(q)*2^q on the negative half-line.
    y_error = F(1,2**20)
    a = (1+y_error)*(1+reciprocal)*(1+u)-1
    b = reciprocal*(1+y_error)*(1+u)
    with localcontext() as ctx:
        ctx.prec = 80
        dec = lambda value: Decimal(value.numerator)/Decimal(value.denominator)
        da, db, dv = map(dec, (a,b,reciprocal))
        ln2 = Decimal(2).ln()
        positive = 2*((ln2*(da+db)).exp()-1) + dv*2*(ln2*(da+db)).exp()
        negative = (ln2*db).exp()*(da/(Decimal(1).exp()*(1-da))+db*ln2+dv)
        power_error = max(positive, negative)
        assert 2*power_error < Decimal(1)/Decimal(2**15)
        du = dec(u)
        # SDR decode: ((c+.055)/1.055)^2.4, with base in [1/20,1].
        base_error = (1+du)**3*(1+dv)/(1-du)-1
        log_error = base_error/((1-base_error)*ln2) + 6*dv
        exponent_error = Decimal('2.4')*(log_error+5*du)+du*Decimal('2.4')*(1+du)*(5+log_error)
        decode_error = ((ln2*exponent_error).exp()-1)+dv*(ln2*exponent_error).exp()
        assert 2*decode_error < Decimal(1)/Decimal(2**15)
        # SDR encode's nonlinear branch: x^(1/2.4), |log2(x)|<9.
        log_error = 10*dv
        exponent_error = (log_error+9*3*du)/Decimal('2.4') + du*(1+3*du)*(9+log_error)/Decimal('2.4')
        power_encode = ((ln2*exponent_error).exp()-1)+dv*(ln2*exponent_error).exp()
        encode_error = Decimal('1.055')*Decimal('1.001')*(power_encode+2*du)+Decimal('1.1')*du
        assert 2*encode_error < Decimal(1)/Decimal(2**15)
    return {'aces_one_path_error': float(curve_error), 'aces_two_path_allowance': 2**-14,
            'filmic_one_path_error': float(normalized_filmic_error), 'filmic_two_path_allowance': 2**-12,
            'signed_output_matrix_error': float(matrix_error), 'matrix_allowance': 2**-17,
            'power_one_path_error': float(power_error), 'power_two_path_allowance': 2**-15,
            'srgb_decode_one_path_error': float(decode_error), 'srgb_encode_one_path_error': float(encode_error),
            'scope': 'Curve/matrix/power arithmetic allowances; native enclosure and timing checks remain required',
            'verdict': 'pass'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = derive()
    print(json.dumps(result, indent=2))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2)+'\n')
